/*
 * ESP32-CAM Low-Latency MJPEG Streaming
 * Board: AI-Thinker ESP32-CAM (OV2640)
 *
 * Key latency optimizations vs the stock CameraWebServer example:
 *  1. fb_count = 2 with CAMERA_GRAB_LATEST -> always serves the newest frame,
 *     drops stale ones instead of queuing (this is the #1 latency killer in
 *     naive implementations that grab whatever's next in a FIFO queue).
 *  2. Lower JPEG quality (higher compression) + smaller frame size ->
 *     less data to encode, transmit, and decode client-side.
 *  3. No delay()/vTaskDelay() in the streaming loop - frames are pushed
 *     as fast as they're available, capped only by network write speed.
 *  4. HTTP chunked multipart response held open on its own task, not
 *     blocking the main loop, so WiFi/TCP stack stays responsive.
 *  5. XCLK at 20MHz (max stable for most OV2640 modules) for fastest
 *     sensor readout.
 *
 * Tune FRAME_SIZE / JPEG_QUALITY below based on your bandwidth budget.
 * Lower quality number = more compression = smaller frames = lower latency,
 * at the cost of visual quality.
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "secrets.h"

// ---------- USER CONFIG ----------
//const char* WIFI_SSID     = "YOUR_SSID";
//const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// Smaller = lower latency. Try FRAMESIZE_QVGA (320x240) first;
// step down to FRAMESIZE_QQVGA (160x120) if you still see lag.
#define FRAME_SIZE     FRAMESIZE_QVGA

// 10-63, lower = more compression = smaller/faster frames, worse quality.
// 12-15 is a good low-latency starting point.
#define JPEG_QUALITY   12
// ----------------------------------

// AI-Thinker ESP32-CAM pin map
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

httpd_handle_t stream_httpd = NULL;

static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// --- Streaming handler: pulls the latest frame and pushes it immediately ---
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    // Prevent any proxy/browser buffering from adding latency
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            // Should not happen given config below, but guard anyway
            esp_camera_fb_return(fb);
            res = ESP_FAIL;
            break;
        }

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }

        // Return the frame buffer immediately after sending -
        // this is what lets the driver reuse it for the NEXT capture
        // instead of stalling on an allocation.
        esp_camera_fb_return(fb);

        if (res != ESP_OK) break;
        // No delay here on purpose - next fb_get() naturally paces us
        // to the sensor's actual frame rate.
    }
    return res;
}

// --- Simple viewer page: live stream + client-side "Save Frame" button ---
// The snapshot happens entirely in the browser (canvas.drawImage on the
// already-received <img> frame -> canvas.toBlob -> download). No request
// ever goes back to the ESP32 for this, so it costs zero streaming latency
// and never contends with the streaming task for the camera.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>ESP32-CAM Live</title>
  <style>
    body { font-family: sans-serif; text-align: center; background:#111; color:#eee; }
    img { max-width: 95vw; border: 2px solid #444; margin-top: 12px; }
    button {
      margin-top: 14px; padding: 10px 22px; font-size: 16px;
      border: none; border-radius: 6px; background:#2d8cf0; color:#fff; cursor:pointer;
    }
    button:active { background:#1c6fd0; }
    #status { margin-top: 8px; font-size: 14px; color:#9a9a9a; }
  </style>
</head>
<body>
  <h2>Live Feed</h2>
  <img id="stream" src="/stream" crossorigin="anonymous">
  <br>
  <button onclick="saveFrame()">Save Frame</button>
  <div id="status"></div>

  <canvas id="canvas" style="display:none;"></canvas>

  <script>
    function saveFrame() {
      const img = document.getElementById('stream');
      const canvas = document.getElementById('canvas');
      const status = document.getElementById('status');

      // Use the image's natural (full sensor) resolution, not its
      // on-screen display size, so the saved frame is full quality.
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;

      const ctx = canvas.getContext('2d');
      ctx.drawImage(img, 0, 0, canvas.width, canvas.height);

      canvas.toBlob(function(blob) {
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        const ts = new Date().toISOString().replace(/[:.]/g, '-');
        a.href = url;
        a.download = 'frame_' + ts + '.jpg';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        status.textContent = 'Saved frame_' + ts + '.jpg';
      }, 'image/jpeg', 0.95);
    }
  </script>
</body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32768;
    // Bump stack size - JPEG chunk sending needs headroom
    config.stack_size = 8192;
    config.max_uri_handlers = 2;

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &index_uri);
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk  = XCLK_GPIO_NUM;
    config.pin_pclk  = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href  = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn  = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;   // 20MHz - fastest stable readout
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        config.frame_size = FRAME_SIZE;
        config.jpeg_quality = JPEG_QUALITY;
        config.fb_count = 2;                       // double buffer
        config.grab_mode = CAMERA_GRAB_LATEST;      // ALWAYS serve newest frame, drop stale ones
        config.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        // No PSRAM: fall back to single small buffer, still low latency
        // but more prone to frame drops under load.
        config.frame_size = FRAMESIZE_QQVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return;
    }

    // Sensor-level tweaks that help latency/throughput
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAME_SIZE);
    s->set_vflip(s, 1);
    // Disable features that add per-frame processing overhead if you
    // don't need them:
    s->set_gainceiling(s, GAINCEILING_2X);
    s->set_bpc(s, false);       // black pixel correction off
    s->set_wpc(s, true);        // white pixel correction (cheap, keep on)
    s->set_lenc(s, true);       // lens correction (cheap, keep on)

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);   // CRITICAL: WiFi modem sleep adds 100-300ms
                              // latency spikes per packet - disable it
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Viewer ready at: http://");
    Serial.print(WiFi.localIP());
    Serial.println(":81/  (raw stream at /stream)");

    startCameraServer();
}

void loop() {
    // Streaming is handled entirely by the HTTP server task.
    delay(10000);
}

import io
from fastapi import FastAPI
from fastapi.responses import StreamingResponse
from picamera2 import Picamera2

app = FastAPI()
cam = Picamera2()
cam.configure(cam.create_video_configuration(main={"size": (1280, 720)}))
cam.start()

def frames():
    while True:
        buf = io.BytesIO()
        cam.capture_file(buf, format="jpeg")
        yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"
               + buf.getvalue() + b"\r\n")

@app.get("/video")
def video():
    return StreamingResponse(
        frames(), media_type="multipart/x-mixed-replace; boundary=frame"
    )
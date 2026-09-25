from picamera2 import Picamera2
import time

cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (1280, 720), "format": "RGB888"}))
cam.start()
time.sleep(1)

frame = cam.capture_array()   # numpy array, OpenCV-ready
cam.capture_file("shot.jpg")
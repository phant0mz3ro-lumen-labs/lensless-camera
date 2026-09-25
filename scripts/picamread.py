import os
os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "fflags;nobuffer|flags;low_delay"
import cv2

cap = cv2.VideoCapture("tcp://192.168.0.104:8888", cv2.CAP_FFMPEG)

while True:
    ok, frame = cap.read()
    if not ok:
        break
    # your pipeline here
    cv2.imshow("stream", frame)
    if cv2.waitKey(1) == 27:
        break
cap.release()
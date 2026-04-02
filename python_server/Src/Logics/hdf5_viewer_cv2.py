import h5py
import cv2

f = h5py.File("sunspot1300 (1).h5", "r")
data = f["data"]

for i in range(1000):  # первые 100 кадров
    frame = data[i]

    cv2.imshow("HDF5 Video", frame)

    # 25 ms ~ 40 FPS (можешь менять)
    if cv2.waitKey(25) & 0xFF == 27:
        break

cv2.destroyAllWindows()
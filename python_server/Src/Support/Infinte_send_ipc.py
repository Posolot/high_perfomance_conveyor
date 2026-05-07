import zmq
import h5py
import numpy as np
import time
import os

IPC_PATH = "ipc:///tmp/frames"

# удалить старый сокет (важно!)
try:
    os.remove("/tmp/frames")
except FileNotFoundError:
    pass

context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.bind(IPC_PATH)

file = h5py.File("sunspot_fullhd.h5", "r")
data = file["data"]

num_frames = data.shape[0]

print(f"Loaded {num_frames} frames")

i = 0

while True:
    frame = data[i]

    t0 = time.time_ns()

    # отправляем timestamp + payload
    socket.send_multipart([
        str(t0).encode(),
        frame.tobytes()
    ])

    i = (i + 1) % num_frames

    # режим нагрузки:
    #time.sleep(0.1)   # ~10 FPS
    #time.sleep(0.02)    
    time.sleep(0.01)  # ~100 FPS
import zmq
import h5py
import numpy as np
import time

# ZMQ setup
context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.bind("tcp://127.0.0.1:5558")

# читаем hdf5
file = h5py.File("sunspot1300 (1).h5", "r")
data = file["data"]  # твой dataset

num_frames = data.shape[0]

print(f"Loaded {num_frames} frames")

i = 0

while True:
    frame = data[i]
    socket.send(frame.tobytes())

    print(f"Sent frame {i}")
    print(f"Sending frame {i}, shape={frame.shape}, dtype={frame.dtype}")
    i = (i + 1) % num_frames

    time.sleep(0.001)
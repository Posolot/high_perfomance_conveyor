import zmq
import numpy as np
import time

# ZMQ setup
context = zmq.Context()
socket = context.socket(zmq.PULL)
socket.connect("tcp://localhost:5558")

HEIGHT = 480
WIDTH = 640
DTYPE = np.uint8

BATCH_SIZE = 100
NUM_BATCHES = 10

print("Receiver started...")

for batch in range(NUM_BATCHES):
    print(f"\nBatch {batch + 1}")

    start_time = time.time()

    for i in range(BATCH_SIZE):
        message = socket.recv()

        frame = np.frombuffer(message, dtype=DTYPE)
        frame = frame.reshape((HEIGHT, WIDTH))

    end_time = time.time()

    duration = end_time - start_time
    fps = BATCH_SIZE / duration

    print(f"Processed {BATCH_SIZE} frames in {duration:.4f} sec")
    print(f"FPS: {fps:.2f}")

print("\nDone. Closing...")

socket.close()
context.term()
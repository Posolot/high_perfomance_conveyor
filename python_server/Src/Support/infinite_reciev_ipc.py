import zmq
import time
import numpy as np

IPC_PATH = "ipc:///tmp/frames"

context = zmq.Context()
socket = context.socket(zmq.PULL)
socket.connect(IPC_PATH)

FRAME_H = 1920
FRAME_W = 1080
CHANNELS = 1

FRAME_SIZE = FRAME_H * FRAME_W * CHANNELS

print("Receiver started (IPC)")

count = 0
start_time = time.time()

latencies = []

last_report = time.time()

while True:
    parts = socket.recv_multipart()

    t_sent = int(parts[0].decode())
    payload = parts[1]

    t_recv = time.time_ns()

    latency_ms = (t_recv - t_sent) / 1e6
    latencies.append(latency_ms)

    # можно распарсить (не обязательно)
    frame = np.frombuffer(payload, dtype=np.uint8)

    count += 1

    now = time.time()

    if now - last_report >= 1.0:
        elapsed = now - start_time
        fps = count / elapsed

        throughput = (count * FRAME_SIZE) / (1024 * 1024) / elapsed

        print("\n===== IPC RECEIVER STATS =====")
        print(f"FPS: {fps:.2f}")
        print(f"Throughput: {throughput:.2f} MB/s")

        if latencies:
            print(f"Avg latency: {np.mean(latencies):.2f} ms")
            print(f"Min latency: {np.min(latencies):.2f} ms")
            print(f"Max latency: {np.max(latencies):.2f} ms")

        # reset окна
        count = 0
        start_time = now
        latencies.clear()
        last_report = now
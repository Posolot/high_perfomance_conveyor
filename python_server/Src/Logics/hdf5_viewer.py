import h5py
import matplotlib.pyplot as plt

# открыть файл
f = h5py.File("sunspot1300 (1).h5", "r")
data = f["data"]

print("Shape:", data.shape)  # (6400, 480, 640)

# показать несколько кадров
frames_to_show = [0, 1, 2, 100, 500]

plt.figure(figsize=(10, 6))

for i, idx in enumerate(frames_to_show):
    plt.subplot(2, 3, i + 1)
    plt.imshow(data[idx], cmap='gray')
    plt.title(f"Frame {idx}")
    plt.axis('off')

plt.tight_layout()
plt.show()
import numpy as np

print("Min:", np.min(data[0]))
print("Max:", np.max(data[0]))
print("Shape одного кадра:", data[0].shape)
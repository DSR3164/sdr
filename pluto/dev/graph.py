import matplotlib.pyplot as plt
import numpy as np

data = np.fromfile("/home/plutoSDR/pluto/dev/symbols.pcm", dtype=np.int16)

samples = []

for x in range(0, len(data), 2):
    samples.append(data[x] + 1j * data[x+1])

ampl = np.abs(samples)
phase = np.angle(samples)
time = np.arange(len(samples))

# plot
plt.subplot(3,1,1)
plt.plot(time, ampl)
plt.grid(True)

plt.subplot(3,1,2)
plt.plot(time, phase)
plt.grid(True)


plt.subplot(3,1,3)
plt.plot(time, data[0::2])
plt.plot(time, data[1::2])
plt.grid(True)

plt.show()

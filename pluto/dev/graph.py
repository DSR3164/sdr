import matplotlib.pyplot as plt
import numpy as np

rx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/rx.pcm", dtype=np.int16)
tx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/tx.pcm", dtype=np.int16)

samples = []
samples1 = []

for x in range(0, len(rx), 2):
    samples.append(rx[x] + 1j * rx[x+1])

for x in range(0, len(rx), 2):
    samples1.append(tx[x] + 1j * tx[x+1])

ampl = np.abs(samples)
phase = np.angle(samples)
time = np.arange(len(samples))

ampl1 = np.abs(samples1)
phase1= np.angle(samples1)
time1 = np.arange(len(samples1))

# plot
plt.subplot(3,1,1)
plt.legend
plt.plot(time, ampl)
plt.grid(True)

plt.subplot(3,1,2)
plt.legend
plt.plot(time, phase)
plt.grid(True)


plt.subplot(3,1,3)
plt.legend
plt.plot(time, rx[0::2])
plt.plot(time, rx[1::2])
plt.grid(True)


# plot
plt.subplot(3,1,1)
plt.legend
plt.plot(time1, ampl1)
plt.grid(True)

plt.subplot(3,1,2)
plt.legend
plt.plot(time1, phase1)
plt.grid(True)


plt.subplot(3,1,3)
plt.legend
plt.plot(time1, tx[0::2])
plt.plot(time1, tx[1::2])
plt.grid(True)
plt.show()
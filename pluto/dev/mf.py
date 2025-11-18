import matplotlib.pyplot as plt
import numpy as np

rx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/rx1.pcm", dtype=np.int16)
tx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/tx1.pcm", dtype=np.int16)
samples1 = []
samples2 = []
for x in range(0, len(rx), 2):
    samples1.append(rx[x]+ 1j * rx[x+1])
for x in range(0, len(tx), 2):
    samples2.append(tx[x]+ 1j * tx[x+1])

samples2 = np.concatenate([np.zeros(4080), samples2])
a = np.ones(10)
time = np.arange(len(samples1))
Is = np.real(samples1)
Qs = np.imag(samples1)
phase1 = np.angle(samples1)
phase2 = np.angle(samples2)
pila = np.convolve(Is, a)
pila2 = np.convolve(Qs, a)

signalI = []
signalQ = []

for x in range(9, len(pila), 10):
    signalI.append(pila[x])
    signalQ.append(pila2[x])

mask = abs(signalQ/max(signalQ)) >= 0.05
signalI = (signalI / max(signalI))[mask]
signalQ = (signalQ / max(signalQ))[mask]
plt.scatter(signalI, signalQ)
plt.axhline()
plt.grid(True)
plt.axvline()
plt.show()

# plot
plt.subplot(3,1,1)
plt.plot(np.arange(len(pila)), pila)
plt.plot(np.arange(len(pila2)), pila2)
plt.legend
plt.grid(True)

plt.subplot(3,1,2)
plt.plot(np.arange(len(phase1)), phase1)
plt.plot(np.arange(len(phase2)), phase2)
plt.legend
plt.grid(True)

plt.subplot(3,1,3)
plt.plot(np.arange(len(Is)), Is)
plt.plot(np.arange(len(Qs)), Qs)
plt.legend
plt.grid(True)
plt.show()
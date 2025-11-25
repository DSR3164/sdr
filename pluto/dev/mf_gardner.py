import matplotlib.pyplot as plt
import numpy as np

def gardner(complex_symbols_after_convolve):
    SPS = 10 # Samples per symbol
    K1 = 0; K2 = 0; p1 = 0; p2 = 0; n = 0
    e = 0
    BnTs = 0.01; Kp = 0.002
    zeta = np.sqrt(2)/2
    teta = ((BnTs)/10)/(zeta + 1/(4*zeta))
    K1 = (-4*zeta*teta)/((1 + 2*zeta*teta + teta**2)*Kp)
    K2 = (-4*teta**2)/((1 + 2*zeta*teta + teta**2)*Kp)
    s = complex_symbols_after_convolve
    err_list = []
    for n in range(0, len(s) - 22):
        e = (np.real(s[n + 10 + 10]) - np.real(s[n + 10])) * np.real(s[n + 10//2 + 10]) + (np.imag(s[n + 10 + 10]) - np.imag(s[n + 10])) * np.imag(s[n + 10//2 + 10])
        e = np.real(e)
        p1 = e*K1
        p2 += p1 + e * K2
        while(p2 > 1):
            p2-=1
        while(p2 < -1):
            p2 = p2 + 1
        offset = int(np.round(p2*SPS))
        err_list.append(e)
    plt.plot(np.arange(len(err_list)), err_list)
    plt.show()
    return offset

rx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/rx1.pcm", dtype=np.int16)
samples_rx = []

for x in range(0, len(rx), 2):
    samples_rx.append((rx[x]+ 1j * rx[x+1])/np.max(rx))


a = np.ones(10)
time = np.arange(len(samples_rx))
Is = np.real(samples_rx)
Qs = np.imag(samples_rx)
pila = np.convolve(Is, a) + 1j*np.convolve(Qs, a)

offset = gardner(pila)
signal = []
for x in range(offset, len(pila), 10):
    signal.append(pila[x])

signal = np.array(signal)
mask = np.abs(signal) >= 0.05
signal = signal[mask]

plt.figure()
plt.subplot(2,1,1)
plt.scatter(np.real(signal), np.imag(signal))
plt.axhline()
plt.grid(True)
plt.axvline()
plt.subplot(2,1,2)
plt.plot(np.arange(len(np.real(signal))), np.real(signal))
plt.plot(np.arange(len(np.imag(signal))), np.imag(signal))
plt.grid()

plt.figure()
plt.xlim([-2, 2])
plt.ylim([-2, 2])
plt.scatter(np.real(samples_rx), np.imag(samples_rx))

plt.figure()
plt.plot(np.arange(len(np.real(signal))), np.real(signal))
plt.plot(np.arange(len(np.imag(signal))), np.imag(signal))
plt.show()

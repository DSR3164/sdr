import numpy as np
import matplotlib.pyplot as plt

Fs = 1
bits = np.random.randint(0, 2, 10).astype(bool)

def mapper(bits):
    I = bits * -2 + 1
    return I.astype(complex)

def pulse_shaping(symbols):
    N = len(symbols)
    samples_per_symbol = 32
    step = N * samples_per_symbol
    t = np.linspace(0, N-N/step, step)
    phase = np.where(np.repeat(symbols, samples_per_symbol) < 0, np.pi, 0) + np.pi/2
    signal = np.cos(2 * np.pi * t + phase)
    return signal, t

symbols = mapper(bits)
signal, t = pulse_shaping(symbols)

t_bits = np.linspace(0, len(bits), len(signal), endpoint=False)
bits_rep = np.repeat(bits, 32).astype(float)

plt.figure(figsize=(10, 4))
plt.plot(t_bits, bits_rep, label="Bits", linewidth=1)
plt.plot(t, signal, label="BPSK", linewidth=1)
plt.legend()
plt.grid(True)
plt.ylim([-2, 2])
plt.title("BPSK Signal and Bits")
plt.xlabel("Time")
plt.ylabel("Amplitude")
plt.show()

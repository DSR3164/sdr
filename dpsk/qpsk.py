import numpy as np
import matplotlib.pyplot as plt

Fs = 1e6 # Sampling frequency
bits = np.random.randint(0, 2, 20).astype(bool) # Random bits

def mapper(bits): 
    I = 2 * bits[0::2] - 1
    Q = 2 * bits[1::2] - 1
    symbols = I.astype(complex) + 1j * Q.astype(complex)

    upsampled_symbols = np.zeros(len(symbols) * 32, dtype=complex)
    upsampled_symbols[::32] = symbols
    return upsampled_symbols

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

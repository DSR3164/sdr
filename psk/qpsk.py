import numpy as np
import matplotlib.pyplot as plt

Fs = 1e6
f_c = 1e5/6
bits = np.random.randint(0, 2, 20)

# QPSK mapping
I = 2*bits[0::2]-1   # in-phase
Q = 2*bits[1::2]-1   # quadrature
symbols = I + 1j*Q

# Upsample
samples_per_symbol = 32
upsampled_symbols = np.zeros(len(symbols)*samples_per_symbol, dtype=complex)
upsampled_symbols[::samples_per_symbol] = symbols

# Pulse shaping
signal_baseband = np.repeat(upsampled_symbols[::samples_per_symbol], samples_per_symbol)
t = np.arange(len(signal_baseband)) / Fs
s = np.real(signal_baseband)*np.cos(2*np.pi*f_c*t) - np.imag(signal_baseband)*np.sin(2*np.pi*f_c*t)
bits_rep = np.repeat(bits, 16)

plt.figure(figsize=(10,4))
plt.plot(t, s, label="QPSK Signal")
plt.plot(t, bits_rep, '--', label="Bits")
plt.plot(t, np.real(signal_baseband), ':', label="I")
plt.plot(t, np.imag(signal_baseband), ':', label="Q")
plt.xlabel("Time [s]")
plt.ylabel("Amplitude")
plt.title("QPSK Signal with Bits")
plt.grid(True)
plt.legend()
plt.show()

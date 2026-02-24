from funcs import *

app.use_app("pyqt5")

rx = np.fromfile("pluto/dev/pcm/ofdm2.pcm", dtype=np.int16)
SPS = 10
bg = 'black'
samples_rx = rx[0::2] + 1j * rx[1::2]
ofdm_symbols_len = 128
cp_len = 32
symbol_len = cp_len + ofdm_symbols_len
symbols_count = len(samples_rx) // (ofdm_symbols_len + cp_len)
print(len(rx))
print("symbols:", symbols_count)

ofdm_symbols = samples_rx.reshape(symbols_count, ofdm_symbols_len + cp_len)
ofdm_symbols_without_cp = []

for x in ofdm_symbols:
    ofdm_symbols_without_cp.append(x[cp_len:])
    print(len(x[cp_len:]))

ofdm_symbols_without_cp = np.array(ofdm_symbols_without_cp).reshape(symbols_count, ofdm_symbols_len)
ofdm_symbols_after_fft = []
print(ofdm_symbols_without_cp.shape)


for x in ofdm_symbols_without_cp:
    ofdm_symbols_after_fft.append(np.fft.fft(x))
ofdm_symbols_after_fft = np.array(ofdm_symbols_after_fft).reshape(symbols_count * ofdm_symbols_len, 1)

vispy_line(ofdm_symbols_after_fft, bgcolor=bg)
vispy_constelation(ofdm_symbols_after_fft, bgcolor=bg)

app.run()

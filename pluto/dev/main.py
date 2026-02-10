from funcs import *

app.use_app("pyqt5")

rx = np.fromfile("/home/plutoSDR/sdr/pluto/dev/rx.pcm", dtype=np.int16)
SPS = 10
bg = 'black'
samples_rx = (rx[0::2] + 1j * rx[1::2])
print(len(rx))
samples_mf = np.convolve(samples_rx, np.ones(SPS))
signal, offset_list = gardner(samples_mf, SPS)
samples_rx = costas_loop(signal)
vispy_line(samples_rx, bgcolor=bg)
vispy_constelation(samples_rx, bgcolor=bg)
app.run()

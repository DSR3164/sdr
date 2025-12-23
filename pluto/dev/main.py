from funcs import *

app.use_app("pyqt6")

rx = np.fromfile("/home/plutoSDR/sdr/pluto/dev/rxtest.pcm", dtype=np.int16)
SPS = 10
bg = 'black'
samples_rx = (rx[0::2] + 1j * rx[1::2])
print(len(rx))
samples_rx = costas_loop(samples_rx)
samples_mf = np.convolve(samples_rx, np.ones(SPS))
signal, offset_list = gardner(samples_mf, SPS)
vispy_line(samples_rx, bgcolor=bg)
app.run()

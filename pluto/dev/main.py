from src.funcs import *
app.use_app("pyqt6")
qt_app = QtWidgets.QApplication([])

rx = np.fromfile("pluto/dev/pcm/rx2.pcm", dtype=np.int16)
bg = 'black'
mx = np.max(rx)
rx = rx / mx
samples_rx = (rx[0::2] + 1j * rx[1::2])
t0 = time.perf_counter()
samples_mf = cpp.convolve_ones(samples_rx) # matched filter - rectangular
samples_mf = cpp.rrc_mf(samples_rx, 0.25, 10, 12) # matched filter - rrc
samples_mf = samples_mf / max(np.max(np.abs(np.real(samples_mf))), np.max(np.abs(np.imag(samples_mf))))
signal, _  = cpp.gardner(samples_mf, 0.026937214601441852)
signal2    = cpp.costas_loop(signal, 0.0030370830013741056)
t1 = time.perf_counter()
vispy_line(samples_rx, title="Constellation after Costas Loop", bgcolor=bg)
dtms = (t1 - t0) * 1e3
print(f"Сэмплов: \033[31m{len(samples_rx):,} | {len(samples_rx)//1920} \033[0mбуферов")
print(f"time: {dtms:,.2f} ms\nrate: \033[32m{(samples_rx.nbytes + samples_mf.nbytes + signal.nbytes)/ (t1 - t0) / 1e9:.2f}\033[0m GB/s")

window = constellaion(filter_type="rrc", lim=False) # QT window
window.show()
app.run()

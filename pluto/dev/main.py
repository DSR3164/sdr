from funcs import *

rx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/rx1.pcm", dtype=np.int16)
SPS = 10
samples_rx = (rx[0::2] + 1j * rx[1::2]) / np.max(rx)
samples_rx = costas_loop(samples_rx)
samples_mf = np.convolve(samples_rx, np.ones(SPS))

right, offset_list, err_list, mean_err = gardner(samples_mf, SPS)
offset_mask = np.array([v + SPS*i for i, v in enumerate(offset_list)])
signal = samples_mf[offset_mask]

plt.figure()
plt.title("Сигнальное созвездие")
plt.scatter(np.real(signal), np.imag(signal))
plt.axhline()
plt.grid(True)
plt.axvline()

plt.figure()
plt.title("Сигнал после согласованного фильтра")
plt.xlim([0, len(signal)])
plt.ylim([-max(np.real(signal))*3/SPS, max(np.real(signal))*3/SPS])
plt.plot(np.arange(len(np.real(signal))), np.real(signal)/SPS)
plt.plot(np.arange(len(np.imag(signal))), np.imag(signal)/SPS)
plt.grid()

plt.figure()
plt.title("Средняя ошибка к сдвигу от правильного сдвига")
plt.axhline()
plt.axvline()
plt.grid()
plt.scatter(np.arange(-SPS//2, SPS//2 + 1), mean_err)
plt.plot(np.arange(-SPS//2, SPS//2 + 1), mean_err)

plt.figure()
plt.title("Ошибка и сдвиг к номеру символа")
plt.grid()
plt.plot(err_list, label='Error')
plt.plot(offset_list, label='Offset')
plt.legend()

plt.show()
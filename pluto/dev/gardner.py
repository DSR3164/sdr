import matplotlib.pyplot as plt
import numpy as np

def gardner(complex_symbols_after_convolve, SPS = 10):
    K1 = 0; K2 = 0; p1 = 0; p2 = 0; e = 0; offset = 0; Kp = 4
    BnTs = 0.01; 
    zeta = np.sqrt(2)/2
    teta = ((BnTs)/10)/(zeta + 1/(4*zeta))
    K1 = (-4*zeta*teta)/((1 + 2*zeta*teta + teta**2)*Kp)
    K2 = (-4*teta**2)/((1 + 2*zeta*teta + teta**2)*Kp)
    s = complex_symbols_after_convolve
    offset_list = []
    err_list = []
    mean_err = []
    for i in range(0, len(s)//SPS-1):
        n = offset
        e  = (np.real(s[n + SPS + SPS*i]) - np.real(s[n + SPS*i])) * np.real(s[n + SPS//2 + SPS*i])
        e += (np.imag(s[n + SPS + SPS*i]) - np.imag(s[n + SPS*i])) * np.imag(s[n + SPS//2 + SPS*i])
        p1 = e*K1
        p2 += p1 + e * K2
        p2 %= 1
        offset = int(np.round(p2*SPS))

        offset_list.append(offset)
        err_list.append(e)
    

    right = 0
    for y in range(0, 2):
        for i in range(right - SPS//2, right + SPS//2+1):
            temp = []
            for x in range(0, len(s) - SPS*2-i, SPS):
                of = x + i
                e  = (np.real(s[of + SPS + SPS]) - np.real(s[of + SPS])) * np.real(s[of + SPS//2 + SPS])
                e += (np.imag(s[of + SPS + SPS]) - np.imag(s[of + SPS])) * np.imag(s[of + SPS//2 + SPS])
                temp.append(e)
            mean_err.append(np.mean(temp))
        right = int(np.round(np.argmin(np.abs(mean_err))))
        if y != 1:
            mean_err.clear()

    offset_list = np.array(offset_list, dtype=int)
    err_list = np.array(err_list, dtype=float)
    mean_err = np.array(mean_err, dtype=float)

    return right, offset_list, err_list, mean_err

rx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/sdr_to_sdr.pcm", dtype=np.int16)
SPS = 10
samples_rx = (rx[0::2] + 1j * rx[1::2]) / np.max(rx)
samples_mf = np.convolve(samples_rx, np.ones(SPS))

right, offset_list, err_list, mean_err = gardner(samples_mf, SPS)
offset_mask = np.array([v + SPS*i for i, v in enumerate(offset_list)])
signal = samples_mf[offset_mask]

plt.figure()
plt.subplot(2,1,1)
plt.title("Сигнальное созвездие")
plt.scatter(np.real(signal), np.imag(signal))
plt.axhline()
plt.grid(True)
plt.axvline()

plt.subplot(2,1,2)
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

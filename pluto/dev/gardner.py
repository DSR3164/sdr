import matplotlib.pyplot as plt
import numpy as np

def gardner(complex_symbols_after_convolve):
    SPS = 10 # Samples per symbol
    K1 = 0; K2 = 0; p1 = 0; p2 = 0; e = 0; offset = 0; Kp = 4
    BnTs = 0.01; 
    zeta = np.sqrt(2)/2
    teta = ((BnTs)/10)/(zeta + 1/(4*zeta))
    K1 = (-4*zeta*teta)/((1 + 2*zeta*teta + teta**2)*Kp)
    K2 = (-4*teta**2)/((1 + 2*zeta*teta + teta**2)*Kp)
    s = complex_symbols_after_convolve
    plt.plot(s)
    plt.show()
    s = s[169250:171800]
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
    
    for i in range(4, 15):
        temp = []
        for x in range(0, len(s) - SPS*2-i, 10):
            of = x + i
            e  = (np.real(s[of + SPS + SPS]) - np.real(s[of + SPS])) * np.real(s[of + SPS//2 + SPS])
            e += (np.imag(s[of + SPS + SPS]) - np.imag(s[of + SPS])) * np.imag(s[of + SPS//2 + SPS])
            temp.append(e)
        mean_err.append(np.mean(temp))

    return offset, offset_list, err_list, mean_err

rx = np.fromfile(f"/home/plutoSDR/sdr/pluto/dev/sdr_to_sdr.pcm", dtype=np.int16)
samples_rx = []

for x in range(0, len(rx), 2):
    samples_rx.append((rx[x]+ 1j * rx[x+1])/np.max(rx))

a = np.ones(10)
time = np.arange(len(samples_rx))
pila = np.convolve(samples_rx, a)

offset, offset_list, err_list, mean_err = gardner(pila)
signal = []
for x in range(0, len(offset_list)-1):
    signal.append(offset_list[x] + 10*x)

signal = np.array(signal)
signal = pila[signal]

print(offset)

plt.figure()
plt.subplot(2,1,1)
plt.title("Сигнальное созвездие")
plt.scatter(np.real(signal), np.imag(signal))
plt.axhline()
plt.grid(True)
plt.axvline()

plt.subplot(2,1,2)
plt.title("Сигнал после согласованного фильтра")
# plt.xlim([5, 192])
plt.ylim([-3, 3])
plt.plot(np.arange(len(np.real(signal))), np.real(signal)/10)
plt.plot(np.arange(len(np.imag(signal))), np.imag(signal)/10)
plt.grid()

plt.figure()
plt.title("Средняя ошибка к сдвигу от правильного сдвига")
plt.axhline()
plt.axvline()
plt.grid()
plt.scatter(np.arange(-5,6), mean_err)
plt.plot(np.arange(-5,6), mean_err)

plt.figure()
plt.title("Ошибка и сдвиг к номеру символа")
plt.grid()
plt.plot(err_list, label='Error')
plt.plot(offset_list, label='Offset')
plt.legend()

plt.show()

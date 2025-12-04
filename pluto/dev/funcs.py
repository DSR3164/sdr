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
            temp.clear()
        right = int(np.round(np.argmin(np.abs(mean_err))))
        if y != 1:
            mean_err.clear()

    offset_list = np.array(offset_list, dtype=int)
    err_list = np.array(err_list, dtype=float)
    mean_err = np.array(mean_err, dtype=float)

    return right, offset_list, err_list, mean_err

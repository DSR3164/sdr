import matplotlib.pyplot as plt
import numpy as np
from vispy import app, scene, plot as vp, visuals
from vispy.scene.visuals import Markers, Line

def gardner(complex_symbols_after_convolve, SPS = 10):
    K1 = 0; K2 = 0; p1 = 0; p2 = 0; e = 0; offset = 0; Kp = 4
    BnTs = 0.01; 
    zeta = np.sqrt(2)/2
    teta = ((BnTs)/10)/(zeta + 1/(4*zeta))
    K1 = (-4*zeta*teta)/((1 + 2*zeta*teta + teta**2)*Kp)
    K2 = (-4*teta**2)/((1 + 2*zeta*teta + teta**2)*Kp)
    s = complex_symbols_after_convolve
    offset_list = []
    fixed = []
    for i in range(0, len(s)//SPS-1):
        n = offset
        e  = (np.real(s[n + SPS + SPS*i]) - np.real(s[n + SPS*i])) * np.real(s[n + SPS//2 + SPS*i])
        e += (np.imag(s[n + SPS + SPS*i]) - np.imag(s[n + SPS*i])) * np.imag(s[n + SPS//2 + SPS*i])
        p1 = e*K1
        p2 += p1 + e * K2
        p2 %= 1
        offset = int(np.round(p2*SPS))

        fixed.append(s[SPS*i + offset])
        offset_list.append(offset)
    offset_list = np.array(offset_list, dtype=int)
    fixed = np.array(fixed)
    return fixed, offset_list

def costas_loop(samples):
    out = np.zeros_like(samples, dtype=complex)

    theta = 0.0
    freq = 0.0

    Kp = 0.02
    Ki = 0.0002

    for n in range(len(samples)):
        r = samples[n] * np.exp(-1j * theta)
        out[n] = r

        I = np.real(r)
        Q = np.imag(r)

        I_hat = np.sign(I)
        Q_hat = np.sign(Q)

        error = I_hat*Q - Q_hat*I
        error = np.clip(error, -1.0, 1.0)

        freq += Ki * error
        theta += freq + Kp * error
        theta = (theta + np.pi) % (2*np.pi) - np.pi

    return out

def find_barker(rx_syms):
    barker = np.array(
        [1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1],
        dtype=np.complex64
    )

    M = len(barker)
    corr = np.zeros(len(rx_syms) - M, dtype=np.complex64)
    
    for i in range(len(corr)):
        temp = np.vdot(barker, rx_syms[i:i+M])
        norm = np.linalg.norm(barker) * np.linalg.norm(rx_syms[i:i+M])
        corr[i]=temp/norm

    mag = np.abs(corr)
    idx = np.argmax(mag)
    return idx, corr, mag

def freq_loop(conv):
    mu = 0
    omega = 0.5
    freq_error = np.zeros(len(conv))
    output_signal = np.zeros(len(conv), dtype=np.complex128)

    for n in range(len(conv)):
        angle_diff = np.angle(conv[n]) - np.angle(output_signal[n-1]) if n > 0 else 0
        freq_error[n] = angle_diff / (2 * np.pi)
        omega = omega + mu * freq_error[n]
        output_signal[n] = conv[n] * np.exp(-1j * omega)
    return output_signal

def vispy_line(samples:np.ndarray, title="Сигнал", bgcolor='white', is_complex:bool = True):
    m = np.max(np.abs(samples))
    l = len(samples)
    x = np.arange(l)
    canvas = scene.SceneCanvas(show=True, title=title, bgcolor=bgcolor)
    view = canvas.central_widget.add_view()
    view.camera = scene.PanZoomCamera()
    if (is_complex):
        view.add(Line(np.c_[x, np.real(samples)], color='blue', width=2))
        view.add(Line(np.c_[x, np.imag(samples)], color='orange', width=2))
    else:
        view.add(Line(np.c_[x, samples], color='blue', width=2))
    view.camera.rect = (0, -m, l, 2*m)

def vispy_constelation(samples:np.ndarray, title = "Сигнальное созвездие", size = 2, edge_color='grey', face_color='blue', bgcolor='white'):
    x = np.max(np.abs(samples))
    canvas = scene.SceneCanvas(keys='interactive', show=True,  title=title, bgcolor=bgcolor)
    view = canvas.central_widget.add_view()
    view.camera = scene.PanZoomCamera(aspect=1)
    view.add(Line(pos=np.array([[-x*2,0],[x*2,0]]), width=2))
    view.add(Line(pos=np.array([[0,-x*2],[0,x*2]]), width=2))
    view.add(Markers(pos = np.c_[np.real(samples), np.imag(samples)], parent=view.scene, size=size, face_color=face_color, edge_color=edge_color))
    view.camera.rect = (-x, -x, x + x, x + x)

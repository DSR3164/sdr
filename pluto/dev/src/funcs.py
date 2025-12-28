import matplotlib.pyplot as plt, numpy as np, build.cpp as cpp, time
from vispy import app, scene, plot as vp, visuals
from vispy.scene.visuals import Markers, Line, Text, Image
from PyQt6 import QtWidgets, QtCore
from PyQt6.QtGui import QAction

class constellaion(QtWidgets.QWidget):
    def __init__(self, filter_type:str="rect", lim:bool = False):
        super().__init__()
        self.filter_type = filter_type
        self.lim = lim
        self.samples_mf = None
        self.gardner_bw = 1.20e-1
        self.costas_bw = 2.88e-3
        self.setWindowTitle("Constellation with slider")
        self.resize(900, 700)
        self.canvas, self.view, self.markers = vispy_constelation(
            np.zeros(10), title="Созвездие", bgcolor='black'
        )
        self.canvas.create_native()
        self.canvas.native.setParent(self)
        self.t = Text("0", 
                    color='white', 
                    font_size=20, 
                    parent=self.canvas.scene, 
                    anchor_x='left', anchor_y='top')
        self.t.transform = scene.STTransform(translate=(100, 100))
        self.slider_gar = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.slider_gar.setMinimum(0)
        self.slider_gar.setMaximum(200)
        self.slider_gar.setValue(0)
        self.slider_gar.valueChanged.connect(self.on_slider_gar)
        self.slider_cos = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.slider_cos.setMinimum(0)
        self.slider_cos.setMaximum(200)
        self.slider_cos.setValue(0)
        self.slider_cos.valueChanged.connect(self.on_slider_cos)
        layout = QtWidgets.QVBoxLayout()
        layout.addWidget(self.canvas.native)
        layout.addWidget(self.slider_gar)
        layout.addWidget(self.slider_cos)
        self.setLayout(layout)
        self.on_slider_gar(self.slider_gar.value())
        self.on_slider_cos(self.slider_cos.value())

        self.menu_bar = QtWidgets.QMenuBar(self)
        file_menu = self.menu_bar.addMenu("File")
        open_action = QAction("Open File", self)
        open_action.triggered.connect(self.open_file)
        file_menu.addAction(open_action)

    def on_slider_gar(self, value):
        min_val = 1.202264e-01 - 1e-1
        max_val = 1.202264e-01 + 1e-1
        log_min = np.log10(min_val)
        log_max = np.log10(max_val)
        log_val = log_min + (log_max - log_min) * (value / 100.0)
        self.gardner_bw = 10**log_val
        self.update_plot()

    def on_slider_cos(self, value):
        min_val = 2.884032e-03 - 1e-3
        max_val = 2.884032e-03 + 1e-3
        log_min = np.log10(min_val)
        log_max = np.log10(max_val)
        log_val = log_min + (log_max - log_min) * (value / 100.0)
        self.costas_bw = 10**log_val
        self.update_plot()

    def update_plot(self):
        if self.samples_mf is None:
            return
        mx = max(np.max(np.real(self.samples_mf)), np.max(np.imag(self.samples_mf)))
        samples_mf = self.samples_mf / mx
        signal, _ = cpp.gardner(samples_mf, self.gardner_bw)
        signal = cpp.costas_loop(signal, self.costas_bw)
        pos = np.c_[np.real(signal), np.imag(signal)]
        self.markers.set_data(pos=pos, size=3,
                              edge_color=(0.33, 0.33, 0.33, 0.15),
                              face_color=(0.66, 0.33, 0.66, 0.75))
        self.t.text = f"Gardner BW: {self.gardner_bw}\nCostas\tBW: {self.costas_bw}"
        self.canvas.update()

    def open_file(self):
        fname, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Open PCM file", "", "PCM Files (*.pcm);;All Files (*)")
        if fname:
            rx = np.fromfile(fname, dtype=np.int16)
            m = np.max(rx)
            rx = rx / (m)
            if (self.filter_type == "rrc"):
                samples_mf = cpp.rrc_mf((rx[0::2] + 1j*rx[1::2]), 0.25, 10, 12)[:int(1e6) if self.lim else None]
                print("RRC")
            else:
                samples_mf = cpp.convolve_ones((rx[0::2] + 1j*rx[1::2]))[:int(1e6) if self.lim else None]
            samples_mf = samples_mf / max(np.max(np.abs(np.real(samples_mf))), np.max(np.abs(np.imag(samples_mf))))
            self.samples_mf = samples_mf
            self.on_slider_cos(self.slider_gar.value())
            self.on_slider_gar(self.slider_cos.value())
            signal, _   = cpp.gardner(self.samples_mf, self.gardner_bw)
            signal      = cpp.costas_loop(signal, self.costas_bw)
            self.markers.set_data(np.c_[np.real(signal), np.imag(signal)], size=4)
            xlim = np.max(np.abs(np.real(self.samples_mf)))
            ylim = np.max(np.abs(np.imag(self.samples_mf)))
            self.lim = lim = max(xlim, ylim)
            self.view.camera.rect = (-lim, -lim, 2*lim, 2*lim)
            self.canvas.update()

def find_barker(rx_syms):
    barker = np.array(
        [1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1],
        dtype=np.complex64
    )

    M = len(barker)
    corr = np.zeros(len(rx_syms), dtype=np.complex64)
    
    for i in range(len(rx_syms)):
        temp = np.vdot(rx_syms, rx_syms)
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
    grid = canvas.central_widget.add_grid()
    view = grid.add_view()
    view.camera = scene.PanZoomCamera()
    x_axis = scene.AxisWidget(orientation='bottom')
    y_axis = scene.AxisWidget(orientation='left')
    grid.add_widget(x_axis, row=0, col=0)
    grid.add_widget(y_axis, row=0, col=0)
    x_axis.link_view(view)
    y_axis.link_view(view)
    if (is_complex):
        view.add(Line(np.c_[x, np.real(samples)], color='blue', width=2))
        view.add(Line(np.c_[x, np.imag(samples)], color='orange', width=2))
    else:
        view.add(Line(np.c_[x, samples], color='blue', width=2))
    view.camera.rect = (0, -m, l, 2*m)

def vispy_constelation(samples: np.ndarray | None,
                       title="Сигнальное созвездие",
                       size=2,
                       edge_color='grey',
                       face_color='blue',
                       bgcolor='white'):

    canvas = scene.SceneCanvas(keys='interactive', show=True, title=title, bgcolor=bgcolor)
    grid = canvas.central_widget.add_grid()
    view = grid.add_view()
    view.camera = scene.PanZoomCamera(aspect=1)
    x_axis = scene.AxisWidget(orientation='bottom')
    y_axis = scene.AxisWidget(orientation='left')
    grid.add_widget(x_axis, row=0, col=0)
    grid.add_widget(y_axis, row=0, col=0)
    x_axis.link_view(view)
    y_axis.link_view(view)
    markers = Markers()
    if samples is not None:
        x = np.max(np.abs(samples))
        view.add(Line(pos=np.array([[-2*x, 0], [2*x, 0]]), width=2))
        view.add(Line(pos=np.array([[0, -2*x], [0, 2*x]]), width=2))
        markers = Markers(
            pos=np.c_[np.real(samples), np.imag(samples)],
            parent=view.scene,
            size=size,
            face_color=face_color,
            edge_color=edge_color
        )
        view.add(markers)
        view.camera.rect = (-x, -x, 2*x, 2*x)
        
    return canvas, view, markers

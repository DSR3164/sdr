import numpy as np
import librosa
from pydub import AudioSegment


#------------MP3==>PCM-------------
y, sr = librosa.load("../1.mp3", sr=44100, mono=False)
if y.ndim == 2:
    y = y.T.reshape(-1)
pcm_data = (y * 32767).astype(np.int16)
pcm_data.tofile("../1.pcm")


#------------PCM==>MP3-------------
pcm_data = np.fromfile("../2.pcm", dtype=np.int16)
mono_stereo = 2
audio = AudioSegment(data=pcm_data.tobytes(), sample_width=2, frame_rate=44100, channels=mono_stereo)
audio.export("../2.mp3", format="mp3", bitrate="159k")

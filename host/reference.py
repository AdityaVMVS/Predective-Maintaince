"""Independent NumPy/SciPy check against a captured three-axis window."""
import numpy as np
from scipy import signal

from .protocol import RATE, WINDOW

FILTER = signal.butter(2, 10, fs=RATE, btype="highpass")


def analyze(raw, axis=2):
    samples = np.asarray(raw, dtype=float)
    if samples.shape != (WINDOW, 3) or axis not in (0, 1, 2):
        raise ValueError("expected 1024 three-axis samples and axis 0, 1 or 2")
    x = samples[:, axis]
    filtered = signal.lfilter(*FILTER, (x - x.mean()) * 0.0039)
    rms = np.sqrt(np.mean(filtered ** 2))
    peak = np.max(np.abs(filtered))
    centered = filtered - filtered.mean()
    variance = np.mean(centered ** 2)
    kurtosis = np.mean(centered ** 4) / variance ** 2 if variance * WINDOW > 1e-10 else 0
    hann = np.hanning(WINDOW)
    frequency = np.fft.rfftfreq(WINDOW, 1 / RATE)
    power = np.abs(np.fft.rfft(filtered * hann) * (2 / hann.sum())) ** 2
    keep = (frequency >= 10) & (frequency <= 600)
    power = np.where(keep, power, 0)
    energy = power.sum()
    dominant = frequency[np.argmax(power)] if energy else 0
    centroid = np.dot(power, frequency) / energy if energy > 1e-12 else 0
    bands = (power[frequency < 100].sum(), power[(frequency >= 100) & (frequency < 300)].sum(),
             power[frequency >= 300].sum())
    return frequency, power, np.array([rms, peak, peak / rms if rms > 1e-8 else 0,
                                       kurtosis, dominant, energy, centroid, *bands])

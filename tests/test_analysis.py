import ctypes as ct
from pathlib import Path

import numpy as np
import pytest

from host.protocol import RATE, WINDOW
from host.reference import analyze


class Features(ct.Structure):
    _fields_ = [("value", ct.c_float * 10)]


class Detector(ct.Structure):
    _fields_ = [(name, ct.c_uint) for name in ("count", "raised", "severe", "shape", "quiet")] + [
        ("mean", ct.c_float * 4), ("sumsq", ct.c_float * 4), ("condition", ct.c_int)
    ]


@pytest.fixture(scope="module")
def native():
    lib = ct.CDLL(str(Path(__file__).resolve().parents[1] / "build/libanalysis.so"))
    lib.analysis_init.restype = ct.c_bool
    lib.analyze.argtypes = [ct.POINTER(ct.c_int16), ct.c_uint]
    lib.analyze.restype = Features
    lib.classify.argtypes = [ct.POINTER(Detector), Features, ct.POINTER(ct.c_float)]
    lib.classify.restype = ct.c_int
    assert lib.analysis_init()
    return lib


T = np.arange(WINDOW) / RATE


def samples(trace, axis=2, dc=0):
    raw = np.zeros((WINDOW, 3), dtype=np.int16)
    raw[:, axis] = np.rint(trace + dc).astype(np.int16)
    return raw


def actual(native, raw, axis):
    return np.array(native.analyze(raw.ctypes.data_as(ct.POINTER(ct.c_int16)), axis).value)


@pytest.mark.parametrize("axis", [0, 1, 2])
@pytest.mark.parametrize("hz", [31.25, 100, 250, 500])
def test_tone(native, axis, hz):
    raw = samples(110 * np.sin(2 * np.pi * hz * T), axis, dc=250)
    result = actual(native, raw, axis)
    _, _, expected = analyze(raw, axis)
    assert result[4] == hz
    np.testing.assert_allclose(result, expected, rtol=0.002, atol=0.00003)


@pytest.mark.parametrize("pattern", ["two_tone", "noise", "dc", "impulse", "quiet", "low_cut"])
def test_reference(native, pattern):
    tones = 100 * np.sin(2 * np.pi * 100 * T) + 65 * np.sin(2 * np.pi * 250 * T)
    wave = {
        "two_tone": tones,
        "noise": np.random.default_rng(4).normal(0, 50, WINDOW),
        "dc": np.full(WINDOW, 350),
        "impulse": np.where(np.arange(WINDOW) == 500, 600, 0),
        "quiet": np.zeros(WINDOW),
        "low_cut": 90 * np.sin(2 * np.pi * 3 * T),
    }[pattern]
    raw = samples(wave)
    result = actual(native, raw, 2)
    hz, power, expected = analyze(raw)
    np.testing.assert_allclose(result, expected, rtol=0.003, atol=0.0003)
    if pattern == "two_tone":
        assert min(power[hz == 100][0], power[hz == 250][0]) > 0.05
    if pattern == "low_cut":
        assert result[0] < 0.05


def classify(native, detector, rms=0.03, frequency=80, energy=0.003, kurtosis=1.5):
    vector = Features((ct.c_float * 10)(rms, 0, 0, kurtosis, frequency, energy))
    score = ct.c_float()
    state = native.classify(ct.byref(detector), vector, ct.byref(score))
    return state, score.value


@pytest.mark.parametrize("values,state", [
    ({}, 1),
    ({"rms": 0.05}, 2),
    ({"rms": 0.2}, 3),
    ({"frequency": 120}, 4),
    ({"kurtosis": 3}, 4),
    ({"energy": 0.01}, 3),
])
def test_alarm_and_recovery(native, values, state):
    detector = Detector()
    for _ in range(64):
        assert classify(native, detector)[0] == 0
    assert classify(native, detector, **values)[0] == 1
    assert classify(native, detector, **values)[0] == 1
    assert classify(native, detector, **values)[0] == state
    for _ in range(3):
        last, score = classify(native, detector)
    assert last == 1 and score == pytest.approx(0, abs=1e-5)


def test_single_spike_does_not_latch(native):
    detector = Detector()
    for _ in range(64):
        classify(native, detector)
    for _ in range(3):
        assert classify(native, detector, rms=0.2)[0] == 1
        assert classify(native, detector)[0] == 1

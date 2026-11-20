"""Display STM32 telemetry, save CSV, and plot a requested raw capture."""
import argparse
from collections import deque
import csv
from pathlib import Path
import time

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.widgets import Button
import numpy as np
from serial import Serial, SerialException

from .protocol import Capture, COLUMNS, Stream
from .reference import analyze


def run(port, log, raw_path):
    writer = csv.DictWriter(log, COLUMNS)
    writer.writeheader()
    stream, capture = Stream(), Capture()
    history = deque(maxlen=180)
    last_receive = time.monotonic()
    last_timestamp = None
    latest = None
    axis = 2
    capture_reference = None
    link_error = ""
    fig, (levels, bands, spectrum) = plt.subplots(3, 1, figsize=(10, 8))
    fig.subplots_adjust(top=0.88, bottom=0.12, hspace=0.65)
    status = fig.text(0.08, 0.98, "Waiting for STM32", va="top")
    capture_button = Button(fig.add_axes([0.12, 0.025, 0.22, 0.045]), "Capture raw")
    learning_button = Button(fig.add_axes([0.39, 0.025, 0.22, 0.045]), "Learn baseline")
    capture_button.on_clicked(lambda _: send(b"R"))
    learning_button.on_clicked(lambda _: send(b"B"))

    def send(command):
        nonlocal link_error
        try:
            port.write(command)
        except SerialException as error:
            link_error = str(error)

    def refresh(_):
        nonlocal last_receive, last_timestamp, latest, axis, capture_reference, link_error
        try:
            incoming = port.read(min(port.in_waiting, 8192))
        except (SerialException, OSError) as error:
            link_error = str(error)
            incoming = b""
        for record in stream.feed(incoming):
            if isinstance(record, dict):
                last_receive = time.monotonic()
                latest = record
                axis = record["axis"]
                writer.writerow(record)
                log.flush()
                if record["state"] in {"SENSOR_FAULT", "ACQUISITION_FAULT"}:
                    continue
                if last_timestamp is not None and record["timestamp_ms"] < last_timestamp:
                    history.clear()
                last_timestamp = record["timestamp_ms"]
                history.append(record)
            else:
                raw = capture.add(record)
                if raw is not None:
                    np.savetxt(raw_path, raw, fmt="%d", delimiter=",", header="x,y,z", comments="")
                    capture_reference = analyze(raw, axis)
        if latest is not None:
            item = latest
            connected = "Connected" if time.monotonic() - last_receive <= 2.5 and not link_error else "No recent data"
            status.set_text(
                f"{connected} · {item['state']} · RMS {item['rms_g']:.3f} g · peak frequency {item['dominant_hz']:.1f} Hz\n"
                f"Baseline {item['trained']}/64 · DSP {item['cycles'] / 16000:.2f} ms · SPI {item['spi_errors']} · "
                f"FIFO {item['overruns']} · drops {item['dropped']} · deadlines {item['deadline_misses']} · "
                f"CRC/format {stream.bad} · gaps {stream.gaps}"
            )
        elif link_error:
            status.set_text(f"Serial connection lost. {link_error}")
        levels.clear()
        bands.clear()
        spectrum.clear()
        if history:
            seconds = [(item["timestamp_ms"] - history[0]["timestamp_ms"]) / 1000 for item in history]
            levels.plot(seconds, [item["rms_g"] for item in history])
            for key, label in (("low_g2", "10–100 Hz"), ("mid_g2", "100–300 Hz"), ("high_g2", "300–600 Hz")):
                bands.plot(seconds, [item[key] for item in history], label=label)
            bands.legend(loc="upper right", fontsize="small")
        levels.set(ylabel="RMS (g)", xlabel="Seconds")
        bands.set(ylabel="Band energy (g²)", xlabel="Seconds")
        if capture_reference is not None:
            hz, power, _ = capture_reference
            spectrum.plot(hz, np.sqrt(power))
        spectrum.set(title="Spectrum from requested capture", ylabel="Amplitude (g)", xlabel="Hz", xlim=(0, 600))

    animation = FuncAnimation(fig, refresh, interval=200, cache_frame_data=False)
    plt.show()
    return animation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--csv", type=Path, default=Path("features.csv"))
    parser.add_argument("--raw", type=Path, default=Path("capture.csv"))
    args = parser.parse_args()
    if args.csv.resolve() == args.raw.resolve():
        parser.error("feature and raw CSV paths must be different")
    try:
        with Serial(args.port, 115200, timeout=0, write_timeout=0.2) as port, args.csv.open("w", newline="") as log:
            run(port, log, args.raw)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()

# Real-Time Vibration Analysis on STM32

I wanted to see whether a small accelerometer could tell when a fan or motor started vibrating differently. The ADXL345 measures movement on three axes, and the STM32 works through the samples as they arrive. It learns what normal vibration looks like and raises an alert when the measurements stay outside that baseline. The laptop shows the results and saves them for later.

This is a condition-monitoring project. An alert points to a change worth checking. Estimating the remaining life of a motor would need measurements from machines tracked through failure.

## Hardware and wiring

Use a NUCLEO-F767ZI, a GY-291 ADXL345 breakout, and a USB cable connected to the Nucleo's ST-LINK port. Mount the sensor firmly on the stationary motor housing or fixture. Keep the wiring short and check the labels on your specific breakout.

| GY-291 | Nucleo MCU pin | Purpose |
| --- | --- | --- |
| VCC | 3.3 V | Power |
| GND | GND | Shared ground |
| SCL / SCLK | PA5 | SPI1 clock |
| SDA / SDI | PB5 | SPI1 MOSI |
| SDO | PA6 | SPI1 MISO |
| CS | PA4 | Active-low chip select |
| INT1 | PB1 | FIFO watermark on EXTI1 |

The firmware uses **PB5 and PB1** for MOSI and INT1. This avoids PA7's Ethernet connection and PB0's onboard LED. These are MCU pin names. Use the board manual to find them on the headers. Supply the breakout and the logic signals at 3.3 V. The ST-LINK virtual COM port carries USART3 on PD8 and PD9, so the telemetry needs no extra wiring.

## Build and run

Install GNU Make, the Arm GNU Toolchain with Newlib and binutils, curl, and Python 3. On Debian or Ubuntu, the Arm packages are typically `gcc-arm-none-eabi`, `binutils-arm-none-eabi`, and `libnewlib-arm-none-eabi`.

```sh
make deps
make
```

Flash `build/monitor.elf` with STM32CubeProgrammer over SWD. If you use `build/monitor.bin`, write it at `0x08000000`. This is a standalone firmware image at the start of flash. Back up any project already on the board before flashing it.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -m host.dashboard --port /dev/ttyACM0 --csv features.csv --raw capture.csv
```

Use your ST-LINK port, such as `COM5` on Windows. The monitor uses 115200 baud, 8N1. Start the motor at its usual steady speed and click **Learn baseline**. Give it about 21 seconds of stable measurements. Click **Capture raw** to save a 1024-sample, three-axis window and plot its spectrum on the laptop. Pressing reset starts a fresh baseline.

By default the STM32 analyzes Z. Run `make AXIS=0` for X or `make AXIS=1` for Y, then reflash. Pick the axis that best captures the vibration on your fixture.

## What runs on the STM32

- SPI reads the ADXL345's three axes at a nominal 1600 samples per second. When the sensor FIFO reaches 16 entries, its interrupt wakes the acquisition task. SPI DMA reads one three-axis sample per transaction.
- A FreeRTOS task processes 1024 samples with 50% overlap. That gives a new feature window every 320 ms and frequency bins 1.5625 Hz apart. It removes the average, applies a 10 Hz high-pass filter, and uses a Hann window and CMSIS-DSP FFT to find the frequency content.
- A baseline compares RMS, dominant frequency, spectral energy, and kurtosis with their normal ranges. Three consecutive unusual windows are needed for an alert. A quiet streak clears it. Sensor and acquisition errors have their own states, and counters help track what went wrong.
- UART sends features, fault counters, and timing. A requested raw capture is sent in small chunks. The dashboard plots trends and checks frame integrity.

## Verification

On Linux, install the Python requirements and a native C compiler, run `make deps`, then run:

```sh
make test
```

The Arm firmware built successfully, and 42 host tests passed. They compare the actual C analysis code with SciPy, exercise the anomaly states, and check serial parsing and capture assembly. Sampling reliability, timing on the Nucleo, and sustained runs with a mounted motor still need to be measured.

- [Architecture, DSP, and detector](docs/design.md)
- [UART and raw capture format](docs/telemetry.md)
- [Bench validation and performance plan](docs/test-plan.md)
- [Portfolio project](https://adityavmvs.github.io/projects/stm32-vibration-monitor.html)

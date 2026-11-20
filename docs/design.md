# Design notes

## Acquisition and task ownership

| Stage | Implementation |
| --- | --- |
| Sensor | Device ID `0xE5`, full-resolution ±8 g, 1600 Hz, FIFO stream mode, 16-sample watermark |
| SPI | SPI1 mode 3 at 4 MHz from the 16 MHz HSI clock, software CS |
| Interrupts | EXTI1 on PB1 for watermark, DMA2 stream 0 RX and stream 3 TX on channel 3 |
| Acquisition | Highest-priority FreeRTOS task, separate task notifications for watermark and DMA completion |
| Analysis | Middle-priority task, one axis per window, feature extraction and classification together |
| Telemetry | Low-priority task, latest feature slot plus optional raw capture |

The EXTI interrupt wakes the acquisition task. An approximately 10 ms timeout also lets the task pick up a watermark that was already high. The task checks the sensor ID and FIFO status, then drains the available samples with one CS assertion per set of X, Y and Z registers. Each set uses a 7-byte DMA transaction including the command byte. CS stays low through the last SPI clock. The driver leaves a gap of at least 5 µs for the FIFO to pop before the next transaction. DMA timeout and error paths disable the streams and reinitialize the sensor. The DMA arrays are placed in SRAM1, outside DTCM, and D-cache is left disabled.

An acquisition-only overlap buffer collects 1024 samples. Once a full window is available, acquisition copies it into a free block and sends the block index to analysis. Analysis returns the index to the free queue when it finishes. Three blocks and all task stacks are statically allocated. The telemetry task receives feature values by copy. A separate capture buffer stays owned by telemetry while chunks are being sent.

A sensor ID, SPI, DMA or sampling-stall fault raises `SENSOR_FAULT`. Overrun, a full FIFO, or exhausting the block pool raises `ACQUISITION_FAULT`. The acquisition task starts a fresh window after reinitializing the sensor. Each gap changes the stream epoch so stale windows cannot update the baseline. A partly learned baseline is restarted after a gap. A completed one is retained, with its consecutive-window counters cleared. The red LED indicates an alert or fault, and the blue LED indicates normal operation.

## Signal processing

The selected axis is averaged over 1024 samples and the mean is removed. The nominal conversion is 0.0039 g per count. A second-order Butterworth high-pass at 10 Hz removes slow changes. The filter starts at zero state on each window, which keeps the embedded and Python implementations directly comparable. The window covers 640 ms and advances by 512 samples, so a new report is due every 320 ms under nominal acquisition.

RMS, peak, crest factor, and population kurtosis come from the filtered samples. For the spectrum, the filtered samples are multiplied by a symmetric Hann window and passed to a 1024-point CMSIS-DSP real FFT. The amplitude scale is twice the inverse Hann sum. Squared amplitude is summed into the experimental bands 10–100 Hz, 100–300 Hz, and 300–600 Hz. A bin on an internal boundary belongs to the higher band. The spectral centroid weights frequency by that squared amplitude.

The band figures are consistent features for comparing windows with the same setup. They depend on the chosen normalization and bands. The 800 Hz Nyquist limit and sensor response constrain which vibrations can be measured.

## Baseline and state transitions

The first 64 complete windows train a RAM-only baseline by updating the running mean and variance. The score uses the absolute standardized deviation of RMS, dominant frequency, spectral energy, and kurtosis, with weights 0.4, 0.15, 0.3 and 0.15 respectively. Each deviation has a denominator floor and is capped at 16 before weighting.

Three consecutive windows with an RMS or energy increase of at least three standard deviations raise `WARNING`. An eight-deviation increase for three windows raises `CRITICAL`. A large frequency, kurtosis, or combined-score change produces `ANOMALY` when the amplitude states do not take priority. Three quiet windows clear the alert. These are initial thresholds for testing. Train again if you change the speed, selected axis, or mount.

DWT counts the elapsed cycles through feature extraction and classification. Higher-priority acquisitions can interrupt that measurement. The deadline counter records cases where a queued window finishes at least 320 ms after acquisition published it.

- [Analog Devices ADXL345 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/adxl345.pdf)
- [Analog Devices FIFO application note](https://www.analog.com/en/resources/app-notes/an-1025.html)
- [ST Nucleo-144 board manual](https://www.st.com/resource/en/user_manual/dm00244518-stm32-nucleo144-boards-mb1137-stmicroelectronics.pdf)
- [STM32F76xxx reference manual](https://www.st.com/resource/en/reference_manual/rm0410-stm32f7-series.pdf)

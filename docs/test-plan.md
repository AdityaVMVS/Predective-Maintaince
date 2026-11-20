# Test plan

## Checks that run without a board

`make test` builds a shared library using the same feature and classifier code as the firmware. The suite exercises the three selectable axes with known tones, verifies noise, impulse, offset and mixed-tone cases against SciPy, checks the detector’s warning and recovery behavior, and tests malformed, missing and fragmented serial data. The Arm build also produces a link map for inspecting flash and RAM use.

## Bench checks

| Test | Method and expected result |
| --- | --- |
| Basic wiring | Verify ID `0xE5` and capture a stationary window. The axis aligned with gravity should read roughly 1 g in raw units. |
| FIFO and rate | Inspect CS, SCLK and INT1 with a logic analyzer. Check for roughly 10 ms between watermarks, 4 MHz SCLK, and one CS transaction per sample. |
| Reference spectrum | Record a raw window at a steady speed and compare its spectrum with the STM32 features from the same capture interval. |
| Normal baseline | Run at a steady speed, request a baseline and watch the state settle to NORMAL. Record operating speed and mounting position. |
| Anomaly response | Apply a repeatable change on a guarded, low-voltage setup and log the latency and state. Keep the rotor and fan blades clear of tools and wires. |
| Sensor recovery | Disconnect and reconnect the sensor with the motor off. The firmware should report SENSOR_FAULT and resume after reinitialization. |
| Lost or delayed acquisition | Use a debugger or a test build to delay servicing the FIFO. Verify the fault counter increases and partial windows are discarded. |
| Telemetry load | Request repeated captures while tracking FIFO overruns, dropped blocks and missed deadlines. |
| Endurance | Log a 24-hour run. Record resets, connection gaps, deadline misses and overflows. |

## Performance results to collect

Record device revision, firmware commit, clock, axis, operating speed and mount in the bench log. Measure the sampling period, DSP cycles, interrupt-to-drain latency, SRAM and flash use, utilization, and fault response time. For CPU utilization, use an idle trace or external timing measurement alongside DWT feature timing.

The acceptance targets are a 320 ms nominal feature interval, DSP completion before the next interval, no acquisition overruns in nominal operation, utilization below 70%, and 24 hours of continuous operation. Hardware supplies the evidence for these targets. The host tests use synthetic signals.

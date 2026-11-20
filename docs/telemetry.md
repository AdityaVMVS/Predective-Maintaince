# Serial telemetry

USART3 goes through the ST-LINK virtual COM port at 115200 baud, 8N1. Each ASCII record is followed by an asterisk, an eight-digit hexadecimal IEEE CRC32 and a newline. The CRC covers the bytes before the asterisk.

A feature record starts with `F`. Fields are comma-separated and appear in this order.

| Fields | Meaning |
| --- | --- |
| Sequence, timestamp | Rolling 32-bit message counter and timestamp in milliseconds |
| State | LEARNING, NORMAL, WARNING, CRITICAL, ANOMALY, SENSOR_FAULT, or ACQUISITION_FAULT |
| Cycles | Elapsed DWT cycles through the analysis task's feature and classifier path |
| SPI errors, FIFO overruns, dropped blocks, deadline misses | Totals since reset |
| Trained, axis | Baseline windows collected and axis index, where X/Y/Z are 0/1/2 |
| Score, RMS, peak, crest, kurtosis, dominant Hz, spectral energy, centroid Hz, low/mid/high energy | Signed integers scaled by 1,000,000 |

Feature values are zero on fault heartbeats. A heartbeat is sent approximately every second if there is no fresh result. USB unplugging or board reset can interrupt the stream. The host uses checksums and feature sequence numbers to report malformed and missing packets.

Send uppercase `B` to relearn from the next valid window. Send uppercase `R` to capture the next valid window. An `R` record carries capture ID, zero-based sample offset, and 48 bytes encoded as 96 hex characters. Each payload contains eight little-endian signed 16-bit X/Y/Z triples. The dashboard requires all 128 chunks in order and saves the most recent complete capture. Feature traffic can appear between capture chunks. The capture spectrum on the laptop uses the independent SciPy path.

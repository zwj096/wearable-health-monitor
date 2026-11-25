# Result packet protocol

The BLE result frame is 22 bytes, little-endian. Valid measurements are emitted every two seconds after a complete 8-second window. Contact loss, charging, sensor faults and sampling discontinuities can emit an earlier status-only frame with the valid bit clear. Recovery requires a new complete window.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | protocol version `1` |
| 2 | 1 | body length `20` (bytes before CRC) |
| 3 | 1 | flags |
| 4 | 2 | sequence number |
| 6 | 4 | window-end timestamp in milliseconds |
| 10 | 2 | heart rate in BPM × 10; zero when state is not valid |
| 12 | 2 | contact temperature in °C × 100, signed |
| 14 | 2 | motion RMS in mg |
| 16 | 1 | quality score, 0–100 |
| 17 | 1 | `whm_state_t` |
| 18 | 2 | battery voltage in mV |
| 20 | 2 | CRC16-CCITT over offsets 0–19 |

Flags are bit 0 valid result, bit 1 no contact, bit 2 charging, bit 3 sensor fault, and bit 4 sampling discontinuity/window rebuild. These names are exposed as `WHM_RESULT_*` constants. Bit 4 also reports an entire-window timing drift exceeding 2% or a sample deviating more than 2.5 ms from its nominal 100 Hz grid position. Receivers must check frame length, magic, version, body length, and CRC before interpreting fields. A displayed heart rate is valid only when state is `WHM_STATE_VALID` and bit 0 is set; an invalid wire value of zero must be displayed as missing, never as a measured zero heart rate.

The temperature field is carried through the simulator CSV and dashboard JSON as `temperature_c`. `heart_rate_valid` describes presentation validity, `heart_rate_bpm` is null for an invalid result, and the raw encoded value is retained separately for protocol inspection. Exported records retain state and flags so invalid windows can be counted rather than silently removed.

# Result packet protocol

The BLE result frame is 22 bytes, little-endian, and is emitted every two seconds after a complete 8-second window.

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

Flags are bit 0 valid result, bit 1 no contact, bit 2 charging, and bit 3 sensor fault. Receivers must check frame length, magic, version, body length, and CRC before interpreting fields.

# System architecture

## Data path

1. Sensor adapters read MAX30102 FIFO frames, BMI270 acceleration, and TMP117 temperature through the I2C HAL.
2. The acquisition task assigns a monotonic timestamp to each sample block and preserves sample-period spacing inside the block.
3. `whm_runtime_push_sample` stores uniformly timed samples in the fixed window and emits a valid measurement every two seconds after the first complete eight-second window. Contact loss, faults, charging and timing discontinuities clear the window and can emit an earlier status-only packet. A timestamp must lie within 2.5 ms of the window's nominal 100 Hz grid; the overall window span is also checked.
4. `whm_estimate_window` removes DC and linear trend, applies a Hann window, evaluates a Goertzel spectrum over 0.5–4 Hz, and interpolates its peak. Both optical channels are independently scored by peak energy concentration; consistent candidates are fused, and a usable single channel incurs a confidence penalty. Three-axis dynamic acceleration reduces quality multiplicatively.
5. `whm_state_step` turns contact, quality, charging, and sensor-fault evidence into an explicit output state.
6. `whm_protocol_encode` serializes the result; the BLE adapter sends it as a notification. Raw PPG is reserved for an explicit debug path.

## Module map

| Module | Responsibility | Hardware dependency |
|---|---|---|
| `whm_ring_buffer` | fixed FIFO/window storage and overflow count | none |
| `whm_signal` | PPG frequency estimate, motion RMS, contact and quality metrics | none |
| `whm_state` | guarded measurement-state transitions | none |
| `whm_protocol` | little-endian packet encoding, decoding and CRC16 | none |
| `whm_runtime` | sampling window, cadence and result assembly | none |
| `whm_hal` | I2C, BLE, monotonic-time and delay contract | board port |
| `whm_sensor_drivers` | MAX30102/BMI270/TMP117 register initialization and decoding | I2C callbacks |
| `whm_controller` | sensor polling, contact/fault flags and BLE handoff | HAL + drivers |
| `app/simulator` | deterministic synthetic input and packet inspection | host only |
| `app/dashboard_server`, `app/dashboard.js` | local JSON API, validity-aware display and CSV export | host only |

## Safety-oriented behavior

- A missing contact flag never becomes a valid heart-rate result.
- A discontinuous, nonuniform or clock-reversed window is rebuilt rather than interpreted at an assumed sampling rate.
- Charging and sensor faults suspend health-parameter output and are represented in state/flags.
- A low-quality window carries a reason-bearing state and zero heart rate rather than a smoothed carry-forward value.
- A corrupted packet fails CRC validation before field use.
- Invalid heart rate is represented as null at the web boundary, displayed as missing, and breaks the plotted line. Temperature is carried through the same CSV/JSON path as other measurements.
- The simulator is a software regression tool; it cannot validate optical coupling, thermal response, RF behavior, battery life, or human accuracy.

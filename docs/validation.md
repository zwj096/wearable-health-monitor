# Validation boundary

## Software-verified

- FIFO ordering and bounded overflow behavior.
- CRC-protected packet round trip and corruption rejection.
- Register-level driver initialization and byte-order decoding through a fake I2C bus.
- End-to-end controller polling from fake sensor reads through a BLE callback.
- Deterministic 75 BPM synthetic PPG estimate inside the configured 0.5–4 Hz band.
- State transitions for contact, baseline, valid, low-quality, charging and recovery.
- Eight-second window fill and two-second output cadence.
- No-contact suppression of a valid heart-rate result.
- Three simulator scenarios compile and produce decodable packets.

## Requires the future board

- MAX30102/BMI270/TMP117 register reads, FIFO timing, clock drift and I2C recovery.
- Optical window, strap pressure, ambient light rejection and thermal coupling.
- BLE radio behavior, notification loss/reconnect, watchdog and power modes.
- Battery charge/discharge behavior, actual LED current and end-to-end power.
- ECG-synchronized human evaluation with participant-level train/test separation.

The repository deliberately reports these as unverified boundaries instead of presenting synthetic simulator output as measured system performance.

# Wearable Health Monitor Software

This repository is the software implementation of the wearable health-monitoring system described in the course design report. It covers the non-physical part of the system: sensor-driver contracts, acquisition timing, timestamp reconstruction, PPG signal processing, motion/contact quality assessment, measurement state management, BLE result framing, host simulation, and automated tests.

The target hardware is an nRF52840 with MAX30102 red/IR PPG, BMI270 three-axis acceleration, and TMP117 contact temperature. Hardware callbacks are isolated in `include/whm_hal.h`; the portable core does not depend on the Nordic SDK and can therefore be built and tested without a board.

## Current scope

- 100 Hz PPG and acceleration samples, 1 Hz temperature updates, 8 s analysis window, 2 s result cadence.
- Fixed-capacity ring buffering with overflow accounting.
- Direct spectral peak search over 0.5–4 Hz, autocorrelation-compatible quality features, motion RMS, contact scoring, and bounded quality output.
- Explicit states: initialization, waiting for contact, baseline establishment, valid measurement, low quality, charging, and fault.
- Versioned 22-byte little-endian result packet with CRC16-CCITT.
- Deterministic simulator modes: `nominal`, `motion`, and `nocontact`.
- Local browser dashboard for the simulator, with metric cards, state display, trend chart, and result-packet table.
- Tests for the FIFO, signal estimator, state machine, packet round trip/CRC, cadence, and no-contact suppression.

The simulator uses synthetic signals. Its numbers are regression-test fixtures, not human measurements or clinical performance claims.

## Build and run

On Linux or macOS with GCC and GNU Make:

```bash
make test
make simulator
make dashboard
```

On the development Windows environment, the Makefile selects `C:/MinGW/bin/gcc.exe` because the system `C:/Windows/gcc.exe` shim cannot start in the sandbox. The same targets work with a normal MinGW installation when `CC` is overridden.

Example modes:

```bash
make test
build/whm_simulator.exe --seconds 12 --mode nominal
build/whm_simulator.exe --seconds 12 --mode motion
build/whm_simulator.exe --seconds 12 --mode nocontact
```

`make dashboard` starts a local server at `http://127.0.0.1:8765`. Open that address in a browser, choose a simulator mode, and click **运行仿真**. The page is self-contained and uses the C simulator output; it does not require a cloud service or external JavaScript package.

## Repository layout

- `include/`: public contracts, HAL callbacks, sensor-driver interfaces, and fixed-size data types.
- `src/`: portable implementation of sensor drivers, controller polling, buffering, signal estimation, runtime orchestration, state transitions, and protocol encoding.
- `app/simulator.c`: host-side deterministic input source and packet decoder.
- `app/dashboard_server.py` and `app/dashboard.html`: local browser dashboard backed by the host simulator.
- `tests/test_core.c`: dependency-free C assertions run by `make test`.
- `docs/architecture.md`: mapping from the course design to software modules and execution flow.
- `docs/protocol.md`: binary BLE result frame and status-bit definitions.
- `docs/validation.md`: what is verified in software and what remains hardware-dependent.
- `platform/nrf52840/README.md`: concrete HAL-to-nRF52840 integration boundary.

## Hardware boundary

A board port supplies I2C register access, monotonic time, BLE notification transport, and optional delay callbacks through `whm_hal_t`. Sensor-specific initialization sequences are kept separate from signal processing so that a driver failure can enter `WHM_STATE_FAULT` instead of being converted into a plausible health value. No code in this repository claims that the hardware has been manufactured or that a person has been measured.

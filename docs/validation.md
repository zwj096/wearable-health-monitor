# Software validation

## Run the checks

```sh
make test
```

The target compiles with C17, warnings-as-errors and no third-party runtime library. It runs:

- `test_core`: FIFO ordering, CRC round trip/corruption, state transitions, controller cadence, no-contact suppression and sensor register contracts. Driver checks use realistic BMI270 raw values and verify mg conversion, the official 8192-byte configuration transfer, initialization status and failure paths. MAX30102 defaults are checked for 100 Hz without decimation and 5 mA optical current.
- `test_signal`: clean off-bin synthetic pulse rates, band edges, red/IR agreement, single-channel degradation, broadband noise, high motion, no contact, historical fault/charging flags and argument bounds.
- `test_runtime_recovery`: missing samples, clock rewind, contact loss, transient faults, undersized packet storage, sustained clock drift and compensating nonuniform sampling. Rejected windows cannot produce a valid heart rate, and recovery requires a fresh complete window.
- `test_dashboard.py`: CSV/JSON temperature preservation, nullable heart rate and raw-value inspection, deterministic temperature changes, scenario responses and rejected query parameters through the local HTTP API.

The clean synthetic rates 71, 75, 83, 101 and 143 bpm are deliberately off the original 7.5 bpm FFT-bin grid (except 75). They check peak interpolation and do not establish accuracy on measured PPG.

## Browser regression

`tests/test_dashboard_ui.py` uses an existing Node/Playwright installation and a Chromium browser. It never installs or downloads packages. Set `WHM_PLAYWRIGHT_PACKAGE` to the absolute path of an existing `playwright-core` package. `WHM_PLAYWRIGHT_NODE` and `WHM_BROWSER` can select an existing Node and Chromium executable; otherwise the test tries the packaged Node and the browser managed by the existing Playwright installation.

```sh
python -B -m unittest discover -s tests -p test_dashboard_ui.py -v
```

With dependencies supplied, the browser checks normal, motion and no-contact states; temperature values from the actual API; missing-heart-rate display; line breaks and real timestamp spacing; CSV content without rerunning simulation; invalid duration; clearing failed results; and a 390-pixel phone viewport. With no package configured, this optional browser test reports a skip rather than pretending it ran.

`docs/dashboard.png` is an actual browser capture of the normal 30-second simulation. A flat heart-rate trace in this deterministic scenario is expected. The slow temperature curve is sampled once per second and retained through the result packet; it is not a measured thermal response.

## Hardware-dependent validation

The portable logic and fake I2C contracts do not verify a built device. A future board still needs checks of FIFO/data-ready service, sensor timing and overflow recovery, actual I2C transactions, optical coupling, ambient light rejection, temperature settling, BLE loss/reconnection, watchdog operation, current and battery life. Heart-rate accuracy and coverage require synchronized reference measurements and participant-level separation of development and evaluation data.

# Wearable Health Monitor Software Implementation Plan

> **For agentic workers:** Execute this plan task by task with test-first checkpoints.

**Goal:** Build a hardware-independent C17 firmware core and host simulator for the designed MAX30102 + BMI270 + TMP117 + nRF52840 wrist health-monitoring system, excluding physical fabrication while covering data acquisition, signal processing, quality assessment, state management, BLE packet encoding, and automated verification.

**Architecture:** Sensor and BLE hardware are isolated behind a HAL interface. The portable core receives timestamped samples, maintains an 8-second window, estimates heart rate from PPG using spectral and autocorrelation evidence, scores signal quality with acceleration and contact signals, drives an explicit measurement state machine, and serializes results into a versioned packet with CRC16. A deterministic host simulator supplies synthetic PPG, motion, contact, temperature, and battery inputs; the same core is exercised by unit tests and CI.

**Tech Stack:** C17, MinGW/GCC locally, POSIX/GCC in GitHub Actions, Makefile, CTest-style test executable without external libraries, Python only for optional log inspection.

**Spec:** `D:/Desktop/研究生课/现代传感/设计课_多传感融合腕式健康监测系统设计_排版优化版.docx` and `D:/Desktop/研究生课/现代传感/后续修改/设计课_天津大学格式/content.py`.

## Global Constraints

- No claims of measured human performance; simulator output is synthetic validation only.
- Preserve the report assumptions: 100 Hz PPG and acceleration, 1 Hz temperature, 8 s window, 2 s output interval, nRF52840 target.
- Keep core code independent from Nordic SDK so it can be compiled and tested on a host without hardware.
- All packets carry sequence, timestamp, quality/state flags, and CRC16; malformed inputs must be rejected.
- New behavior is introduced test-first: each test must fail for the intended reason before production implementation.

---

### Task 1: Project skeleton and contract tests

**Files:**
- Create: `Makefile`, `.gitignore`, `.github/workflows/ci.yml`
- Create: `include/whm_types.h`, `include/whm_ring_buffer.h`, `include/whm_protocol.h`, `include/whm_state.h`, `include/whm_signal.h`, `include/whm_runtime.h`, `include/whm_hal.h`
- Create: `tests/test_core.c`

**Interfaces:**
- `whm_sample_t`, `whm_result_t`, `whm_state_t`, and `whm_status_flags_t` are the shared data contracts.
- Tests call `whm_ring_push/pop`, `whm_protocol_encode/decode`, `whm_crc16`, `whm_state_step`, and `whm_estimate_window`.

- [ ] Write failing tests for ring-buffer ordering, CRC rejection, state transitions, and synthetic heart-rate estimation.
- [ ] Run `make test` and confirm failure because implementations are absent.
- [ ] Add the minimal headers and build rules needed to compile the test harness.

### Task 2: Ring buffer and deterministic signal estimator

**Files:**
- Create: `src/whm_ring_buffer.c`, `src/whm_signal.c`
- Modify: `tests/test_core.c`

**Interfaces:**
- `whm_ring_buffer_t` stores fixed-size `whm_sample_t` entries without heap allocation.
- `whm_estimate_window(const whm_sample_t*, size_t, whm_estimate_t*)` returns heart rate, motion RMS, contact score, and quality score.

- [ ] Add a failing test for FIFO overflow behavior and 75 BPM synthetic PPG.
- [ ] Implement fixed-capacity FIFO with explicit overflow count.
- [ ] Implement DC removal, AC amplitude checks, motion RMS, DFT peak search over 0.5–4 Hz, autocorrelation peak search, and bounded quality scoring.
- [ ] Run focused tests and then the complete test executable.

### Task 3: State machine and BLE result packet

**Files:**
- Create: `src/whm_state.c`, `src/whm_protocol.c`
- Modify: `tests/test_core.c`

**Interfaces:**
- `whm_state_step` transitions among INIT, WAIT_CONTACT, BASELINE, VALID, LOW_QUALITY, CHARGING, and FAULT.
- `whm_protocol_encode` and `whm_protocol_decode` serialize/validate a fixed binary result frame.

- [ ] Add failing tests for contact warm-up, valid output, low-quality fallback, charging pause, and CRC corruption.
- [ ] Implement guarded transitions and result flags.
- [ ] Implement little-endian serialization and CRC16-CCITT.
- [ ] Run all tests with warnings treated as errors.

### Task 4: Runtime integration and host simulator

**Files:**
- Create: `src/whm_runtime.c`, `app/simulator.c`
- Modify: `include/whm_runtime.h`, `tests/test_core.c`

**Interfaces:**
- `whm_runtime_init`, `whm_runtime_push_sample`, and `whm_runtime_tick` implement the 100 Hz/1 Hz acquisition schedule, 8 s rolling window, and 2 s result cadence.
- The simulator emits deterministic CSV and packet summaries for nominal, motion-corrupted, and no-contact scenarios.

- [ ] Add failing integration tests for output cadence and no-contact suppression.
- [ ] Implement runtime orchestration and packet production.
- [ ] Implement simulator command-line modes and reproducible seed.
- [ ] Run simulator and verify no fabricated “measured” claims appear in logs.

### Task 5: Sensor drivers and controller integration

**Files:**
- Create: `include/whm_sensor_drivers.h`, `src/whm_sensor_drivers.c`
- Create: `include/whm_controller.h`, `src/whm_controller.c`
- Modify: `tests/test_core.c`

- [ ] Test MAX30102 FIFO, BMI270 signed acceleration, and TMP117 big-endian temperature through a fake I2C HAL.
- [ ] Implement register initialization, byte-order conversion, sample contact/fault flags, and BLE callback handoff.
- [ ] Run the complete test executable.

### Task 6: Documentation and release verification

**Files:**
- Create: `README.md`, `docs/architecture.md`, `docs/protocol.md`, `docs/validation.md`
- Modify: `.github/workflows/ci.yml`, `Makefile`

- [ ] Document module responsibilities, hardware mapping, build commands, packet fields, and limitations.
- [ ] Add CI jobs for `make test` and `make simulator`.
- [ ] Run local build, test, simulator, and repository cleanliness checks.
- [ ] Commit the complete project locally and prepare it for the user’s GitHub remote.

### Upload gate

After local verification, inspect the GitHub remote and authentication. If no repository URL or GitHub credential is available, stop only at the upload step and request the target repository URL or an authenticated remote; do not invent a destination or expose credentials.

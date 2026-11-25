# nRF52840 port boundary

The portable core intentionally does not embed Nordic SDK headers. A board port should implement the callbacks in `whm_hal_t` with the project’s chosen SDK version:

- `i2c_write` / `i2c_read`: one nrfx TWIM transaction for MAX30102 (`0x57`), BMI270 (`0x68` or `0x69`) and TMP117 (`0x48`).
- `monotonic_time_us`: a 1 MHz free-running timer or RTC-to-microsecond conversion. The timestamp belongs to the sample block; it must not be copied from the FIFO interrupt arrival time for every frame.
- `delay_ms`: a required bounded startup delay after sensor reset and during BMI270 configuration loading. Initialization fails if it is absent.
- `ble_send`: a notification path that reports backpressure or a disconnected link to the application state machine.

The expected board loop is:

1. Initialize clocks, GPIO, TWIM, timer and BLE.
2. Create three `whm_i2c_device_t` values with the shared HAL.
3. Call `whm_controller_init`.
   This configures MAX30102 for 100 Hz without FIFO averaging/decimation and a 5 mA initial current in each optical channel. BMI270 receives the bundled official 8 KiB configuration, requires a successful initialization status, and uses a +/-4 g range. Its read interface returns mg rather than raw LSB.
4. Schedule `whm_controller_poll` at 100 Hz from a timer/task context.
5. Route `last_packet` only through `ble_send`; keep raw waveforms on an explicit debug characteristic.
6. Enter `WHM_STATE_FAULT` on an I2C timeout, invalid chip ID, FIFO overrun or repeated BLE backpressure.

The repository provides the driver and controller logic up to this boundary. Actual nrfx initialization, pin assignment, linker configuration and radio credentials remain board/project-specific and are not claimed as tested here.

The 100 Hz timestamp grid must describe sensor sample time. Use FIFO sample counters and a reconstructed uniform time base when servicing blocks; do not treat an arbitrary host polling time as an evenly spaced sensor timestamp. The runtime rejects gaps, rewinds and accumulated nonuniform timing. The present controller demonstrates one-frame polling; a board FIFO implementation must additionally detect empty FIFO, overflow and missed frames before calling the portable runtime.

BMI270 configuration data is from the official [Bosch SensorAPI](https://github.com/boschsensortec/BMI270_SensorAPI/tree/41129fcfe39c583ee5462d79195741945d51c1fe), with its BSD license retained in `vendor/bosch/LICENSE`. Optical register settings follow the [MAX30102 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf).

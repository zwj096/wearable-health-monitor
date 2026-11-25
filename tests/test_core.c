#include "whm_protocol.h"
#include "whm_ring_buffer.h"
#include "whm_runtime.h"
#include "whm_sensor_drivers.h"
#include "whm_controller.h"
#include "whm_signal.h"
#include "whm_state.h"
#include "test_support.h"

#include <assert.h>
#include <stdio.h>

static whm_sample_t sample_at(double t, bool contact) {
    whm_sample_t sample = {0};
    sample.timestamp_us = (uint64_t)(t * 1000000.0);
    sample.ppg_red = whm_test_ppg(t, 1.25);
    sample.ppg_ir = whm_test_ppg(t, 1.25);
    sample.accel_z = 1000;
    sample.temperature_c = 33.5f;
    sample.battery_mv = 3800;
    sample.flags = contact ? WHM_SAMPLE_CONTACT_OK : 0;
    return sample;
}

static void test_ring_fifo_and_overflow(void) {
    whm_ring_buffer_t ring;
    whm_ring_init(&ring, 3);
    for (int i = 0; i < 4; ++i) {
        whm_sample_t sample = {0};
        sample.ppg_red = i;
        assert(whm_ring_push(&ring, &sample));
    }
    assert(whm_ring_count(&ring) == 3);
    assert(ring.overflow_count == 1);
    whm_sample_t out = {0};
    assert(whm_ring_pop(&ring, &out) && out.ppg_red == 1);
    assert(whm_ring_pop(&ring, &out) && out.ppg_red == 2);
    assert(whm_ring_pop(&ring, &out) && out.ppg_red == 3);
    assert(!whm_ring_pop(&ring, &out));
}

static void test_protocol_round_trip_and_crc(void) {
    const whm_result_t source = {42, 123456, 755, 3350, 18, 87, WHM_STATE_VALID, 3810, 3};
    whm_result_t decoded = {0};
    uint8_t packet[WHM_PACKET_SIZE] = {0};
    assert(whm_protocol_encode(&source, packet, sizeof(packet)) == WHM_PACKET_SIZE);
    assert(whm_protocol_decode(packet, sizeof(packet), &decoded));
    assert(decoded.sequence == source.sequence);
    assert(decoded.timestamp_ms == source.timestamp_ms);
    assert(decoded.heart_rate_bpm_x10 == source.heart_rate_bpm_x10);
    assert(decoded.temperature_c_x100 == source.temperature_c_x100);
    assert(decoded.motion_mg == source.motion_mg);
    assert(decoded.quality == source.quality);
    assert(decoded.state == source.state);
    assert(decoded.battery_mv == source.battery_mv);
    assert(decoded.flags == source.flags);
    packet[10] ^= 0x01u;
    assert(!whm_protocol_decode(packet, sizeof(packet), &decoded));
}

static void test_state_transitions(void) {
    whm_state_machine_t machine;
    whm_state_init(&machine);
    whm_state_input_t input = {0};
    whm_state_step(&machine, &input);
    assert(machine.state == WHM_STATE_WAIT_CONTACT);
    input.contact_ok = true;
    whm_state_step(&machine, &input);
    assert(machine.state == WHM_STATE_BASELINE);
    input.signal_ok = true;
    whm_state_step(&machine, &input);
    assert(machine.state == WHM_STATE_VALID);
    input.signal_ok = false;
    whm_state_step(&machine, &input);
    assert(machine.state == WHM_STATE_LOW_QUALITY);
    input.charging = true;
    whm_state_step(&machine, &input);
    assert(machine.state == WHM_STATE_CHARGING);
    input.charging = false;
    whm_state_step(&machine, &input);
    assert(machine.state == WHM_STATE_WAIT_CONTACT);
}

static void test_signal_estimator(void) {
    whm_sample_t samples[WHM_WINDOW_SAMPLES];
    for (size_t i = 0; i < WHM_WINDOW_SAMPLES; ++i) {
        samples[i] = sample_at((double)i / WHM_SAMPLE_RATE_HZ, true);
    }
    whm_estimate_t estimate = {0};
    assert(whm_estimate_window(samples, WHM_WINDOW_SAMPLES, &estimate));
    assert(estimate.valid);
    assert(estimate.heart_rate_bpm > 72.0f && estimate.heart_rate_bpm < 78.0f);
    assert(estimate.signal_quality > 0.5f);
}

static void test_runtime_output_cadence(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    uint8_t packet[WHM_PACKET_SIZE] = {0};
    size_t packet_length = 0u;
    unsigned outputs = 0u;
    for (size_t i = 0; i < 1100u; ++i) {
        const whm_sample_t sample = sample_at((double)i / WHM_SAMPLE_RATE_HZ, true);
        if (whm_runtime_push_sample(&runtime, &sample, packet, sizeof(packet), &packet_length)) {
            whm_result_t result = {0};
            assert(packet_length == WHM_PACKET_SIZE);
            assert(whm_protocol_decode(packet, packet_length, &result));
            assert(result.timestamp_ms == (uint32_t)(sample.timestamp_us / 1000u));
            outputs++;
        }
    }
    assert(outputs == 2u);
}

static void test_runtime_suppresses_no_contact(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    uint8_t packet[WHM_PACKET_SIZE] = {0};
    size_t packet_length = 0u;
    for (size_t i = 0; i < 1000u; ++i) {
        const whm_sample_t sample = sample_at((double)i / WHM_SAMPLE_RATE_HZ, false);
        if (whm_runtime_push_sample(&runtime, &sample, packet, sizeof(packet), &packet_length)) {
            whm_result_t result = {0};
            assert(whm_protocol_decode(packet, packet_length, &result));
            assert(result.state != WHM_STATE_VALID);
            assert(result.heart_rate_bpm_x10 == 0u);
        }
    }
}

typedef struct {
    uint8_t selected_register;
    unsigned writes;
    uint8_t chip_id;
    uint8_t fifo[6];
    uint8_t accel[6];
    uint8_t temperature[2];
    uint8_t bmi_registers[128];
    uint8_t bmi_config[8192];
    uint8_t max_registers[256];
    size_t bmi_config_bytes;
    size_t fail_config_after_bytes;
    unsigned elapsed_ms;
    unsigned aps_disabled_ms;
    unsigned config_completed_ms;
    unsigned config_completions;
    bool config_loading;
    bool config_address_set;
    bool config_complete;
    bool status_checked;
    bool force_internal_status;
    bool fail_status_read;
    uint8_t internal_status;
    size_t config_address;
} fake_i2c_bus_t;

static fake_i2c_bus_t fake_bus;
static uint64_t fake_time_us;
static unsigned fake_ble_count;
static bool fake_fifo_synthetic;
static size_t fake_fifo_index;

static bool fake_i2c_write(uint8_t address, const uint8_t *data, size_t length) {
    assert(length >= 1u);
    fake_bus.selected_register = data[0];
    fake_bus.writes++;
    if (address == 0x57u && length > 1u) {
        assert((size_t)data[0] + length - 1u <= sizeof(fake_bus.max_registers));
        for (size_t i = 1u; i < length; ++i) fake_bus.max_registers[data[0] + i - 1u] = data[i];
    }
    if (address == 0x68u && length > 1u) {
        const uint8_t reg = data[0];
        if (reg == 0x7eu && length == 2u && data[1] == 0xb6u) {
            fake_bus.bmi_config_bytes = 0u;
            fake_bus.config_loading = false;
            fake_bus.config_complete = false;
            fake_bus.config_address_set = false;
            fake_bus.status_checked = false;
            fake_bus.config_completions = 0u;
            for (size_t i = 0; i < sizeof(fake_bus.bmi_registers); ++i) fake_bus.bmi_registers[i] = 0u;
        } else if (reg == 0x7cu && length == 2u && data[1] == 0u) {
            fake_bus.aps_disabled_ms = fake_bus.elapsed_ms;
        } else if (reg == 0x59u && length == 2u && data[1] == 0u) {
            if (fake_bus.elapsed_ms <= fake_bus.aps_disabled_ms) return false;
            fake_bus.config_loading = true;
        } else if (reg == 0x5bu && length == 3u) {
            if (!fake_bus.config_loading || fake_bus.config_complete || data[1] > 0x0fu) return false;
            fake_bus.config_address = ((size_t)data[2] * 16u + data[1]) * 2u;
            fake_bus.config_address_set = true;
        } else if (reg == 0x5eu) {
            const size_t count = length - 1u;
            if (!fake_bus.config_loading || !fake_bus.config_address_set || fake_bus.config_complete ||
                count % 2u != 0u || fake_bus.config_address != fake_bus.bmi_config_bytes ||
                count > sizeof(fake_bus.bmi_config) - fake_bus.bmi_config_bytes ||
                (fake_bus.fail_config_after_bytes != 0u && fake_bus.bmi_config_bytes >= fake_bus.fail_config_after_bytes)) return false;
            for (size_t i = 0; i < count; ++i) fake_bus.bmi_config[fake_bus.config_address + i] = data[i + 1u];
            fake_bus.bmi_config_bytes += count;
            fake_bus.config_address_set = false;
            return true;
        } else if (reg == 0x59u && length == 2u && data[1] == 1u) {
            if (!fake_bus.config_loading || fake_bus.bmi_config_bytes != sizeof(fake_bus.bmi_config) || fake_bus.config_complete) return false;
            fake_bus.config_complete = true;
            fake_bus.config_completed_ms = fake_bus.elapsed_ms;
            fake_bus.config_completions++;
        } else if (reg == 0x7du && length == 2u && data[1] == 0x04u) {
            if (!fake_bus.status_checked || fake_bus.bmi_registers[0x40u] != 0xa8u || fake_bus.bmi_registers[0x41u] != 0x01u) return false;
        }
        if ((size_t)reg + length - 1u > sizeof(fake_bus.bmi_registers)) return false;
        for (size_t i = 1u; i < length; ++i) fake_bus.bmi_registers[reg + i - 1u] = data[i];
    }
    return true;
}

static bool fake_i2c_read(uint8_t address, uint8_t *data, size_t length) {
    if (address == 0x68u && fake_bus.selected_register == 0x21u && length == 1u) {
        if (fake_bus.fail_status_read) return false;
        data[0] = fake_bus.force_internal_status ? fake_bus.internal_status :
                  (fake_bus.config_complete && fake_bus.elapsed_ms - fake_bus.config_completed_ms >= 20u ? 1u : 0u);
        fake_bus.status_checked = (data[0] & 0x0fu) == 1u;
    } else if (fake_bus.selected_register == 0x00u && length == 1u) {
        data[0] = fake_bus.chip_id;
    } else if (fake_bus.selected_register == 0x07u && length == sizeof(fake_bus.fifo)) {
        if (fake_fifo_synthetic) {
            const double t = (double)fake_fifo_index++ / WHM_SAMPLE_RATE_HZ;
            const uint32_t red = (uint32_t)whm_test_ppg(t, 1.25);
            const uint32_t ir = (uint32_t)whm_test_ppg(t, 1.25);
            data[0] = (uint8_t)(red >> 16u); data[1] = (uint8_t)(red >> 8u); data[2] = (uint8_t)red;
            data[3] = (uint8_t)(ir >> 16u); data[4] = (uint8_t)(ir >> 8u); data[5] = (uint8_t)ir;
        } else {
            for (size_t i = 0; i < length; ++i) data[i] = fake_bus.fifo[i];
        }
    } else if (address == 0x68u && fake_bus.selected_register == 0x0cu && length == sizeof(fake_bus.accel)) {
        for (size_t i = 0; i < length; ++i) data[i] = fake_bus.accel[i];
    } else if (fake_bus.selected_register == 0x00u && length == sizeof(fake_bus.temperature)) {
        for (size_t i = 0; i < length; ++i) data[i] = fake_bus.temperature[i];
    } else {
        return false;
    }
    return true;
}

static void fake_delay_ms(uint32_t milliseconds) {
    fake_bus.elapsed_ms += milliseconds;
}

static uint64_t fake_monotonic_time_us(void) {
    return fake_time_us;
}

static bool fake_ble_send(const uint8_t *data, size_t length) {
    assert(data != NULL && length == WHM_PACKET_SIZE);
    fake_ble_count++;
    return true;
}

static void test_controller_poll_path(void) {
    fake_bus = (fake_i2c_bus_t){0};
    fake_fifo_synthetic = true;
    fake_fifo_index = 0u;
    fake_bus.chip_id = 0x24u;
    fake_bus.fifo[0] = 0x01u; fake_bus.fifo[1] = 0x23u; fake_bus.fifo[2] = 0x45u;
    fake_bus.fifo[3] = 0x02u; fake_bus.fifo[4] = 0x34u; fake_bus.fifo[5] = 0x56u;
    fake_bus.accel[0] = 0x00u; fake_bus.accel[1] = 0x00u;
    fake_bus.accel[2] = 0x00u; fake_bus.accel[3] = 0x00u;
    fake_bus.accel[4] = 0x00u; fake_bus.accel[5] = 0x20u; /* 1 g at +/-4 g. */
    fake_bus.temperature[0] = 0x0du; fake_bus.temperature[1] = 0x48u;
    fake_time_us = 0u;
    fake_ble_count = 0u;
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, fake_ble_send, fake_monotonic_time_us, fake_delay_ms};
    whm_controller_t controller;
    assert(whm_controller_init(&controller, &hal,
                               (whm_i2c_device_t){&hal, 0x57u},
                               (whm_i2c_device_t){&hal, 0x68u},
                               (whm_i2c_device_t){&hal, 0x48u},
                               3800u));
    assert((fake_bus.max_registers[0x08u] >> 5u) == 0u); /* No FIFO rate decimation. */
    assert(fake_bus.max_registers[0x0cu] == 0x19u && fake_bus.max_registers[0x0du] == 0x19u); /* 5 mA each. */
    for (unsigned i = 0u; i < 900u; ++i) {
        fake_time_us = (uint64_t)i * 10000u;
        assert(whm_controller_poll(&controller));
    }
    assert(fake_ble_count >= 1u);
    whm_result_t result = {0};
    assert(whm_protocol_decode(controller.last_packet, controller.last_packet_length, &result));
    assert(result.state == WHM_STATE_VALID);
    assert(result.heart_rate_bpm_x10 > 740u && result.heart_rate_bpm_x10 < 760u);
}
static void test_sensor_driver_register_contracts(void) {
    fake_bus = (fake_i2c_bus_t){0};
    fake_fifo_synthetic = false;
    fake_bus.chip_id = 0x24u;
    fake_bus.fifo[0] = 0x01u; fake_bus.fifo[1] = 0x23u; fake_bus.fifo[2] = 0x45u;
    fake_bus.fifo[3] = 0x02u; fake_bus.fifo[4] = 0x34u; fake_bus.fifo[5] = 0x56u;
    fake_bus.accel[0] = 0x00u; fake_bus.accel[1] = 0x20u; /* 8192 LSB = 1000 mg. */
    fake_bus.accel[2] = 0x00u; fake_bus.accel[3] = 0xe0u; /* -8192 LSB = -1000 mg. */
    fake_bus.accel[4] = 0x00u; fake_bus.accel[5] = 0x10u; /* 4096 LSB = 500 mg. */
    fake_bus.temperature[0] = 0x0du; fake_bus.temperature[1] = 0x48u;
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, NULL, NULL, fake_delay_ms};
    const whm_i2c_device_t max = {&hal, 0x57u};
    const whm_i2c_device_t bmi = {&hal, 0x68u};
    const whm_i2c_device_t tmp = {&hal, 0x48u};
    const whm_max30102_config_t max_config = {2u, 0x1fu};
    assert(whm_max30102_init(&max, &max_config));
    int32_t red = 0; int32_t ir = 0;
    assert(whm_max30102_read_fifo(&max, &red, &ir));
    assert(red == 0x12345 && ir == 0x23456);
    assert(whm_bmi270_init(&bmi));
    int16_t x = 0; int16_t y = 0; int16_t z = 0;
    assert(whm_bmi270_read_accel(&bmi, &x, &y, &z));
    assert(x == 1000 && y == -1000 && z == 500);
    assert(whm_tmp117_init(&tmp));
    float temperature = 0.0f;
    assert(whm_tmp117_read_temperature_c(&tmp, &temperature));
    assert(temperature > 26.5f && temperature < 26.7f);
    assert(fake_bus.writes > 10u);
}

static void test_acceleration_mg_rounding_and_limits(void) {
    /* Catch raw-count output, truncation toward zero and 16-bit overflow. */
    const int16_t raw[] = {0, 4, -4, 5, -5, 512, -512, 4096, -4096, 8192, -8192, 32767, -32768};
    const int16_t expected_mg[] = {0, 0, 0, 1, -1, 63, -63, 500, -500, 1000, -1000, 4000, -4000};
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, NULL, NULL, NULL};
    const whm_i2c_device_t bmi = {&hal, 0x68u};
    fake_bus = (fake_i2c_bus_t){0};
    for (size_t i = 0; i < sizeof(raw) / sizeof(raw[0]); ++i) {
        const uint16_t bits = (uint16_t)raw[i];
        for (size_t axis = 0; axis < 3u; ++axis) {
            fake_bus.accel[2u * axis] = (uint8_t)bits;
            fake_bus.accel[2u * axis + 1u] = (uint8_t)(bits >> 8u);
        }
        int16_t x = 123; int16_t y = 123; int16_t z = 123;
        assert(whm_bmi270_read_accel(&bmi, &x, &y, &z));
        assert(x == expected_mg[i] && y == expected_mg[i] && z == expected_mg[i]);
    }
}

static void test_max30102_rejects_reserved_sample_averages(void) {
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, NULL, NULL, NULL};
    const whm_i2c_device_t max = {&hal, 0x57u};
    fake_bus = (fake_i2c_bus_t){0};
    assert(!whm_max30102_init(&max, &(whm_max30102_config_t){6u, 0x1fu}));
    assert(!whm_max30102_init(&max, &(whm_max30102_config_t){7u, 0x1fu}));
    assert(fake_bus.writes == 0u);
    assert(whm_max30102_init(&max, &(whm_max30102_config_t){5u, 0x1fu}));
}

static void test_bmi270_uploads_configuration_before_enabling_acceleration(void) {
    /* Catch omitted/truncated uploads, byte-vs-word addressing and wrong range. */
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, NULL, NULL, fake_delay_ms};
    const whm_i2c_device_t bmi = {&hal, 0x68u};
    fake_bus = (fake_i2c_bus_t){0};
    fake_bus.chip_id = 0x24u;
    assert(whm_bmi270_init(&bmi));
    assert(fake_bus.bmi_config_bytes == 8192u);
    uint32_t config_checksum = UINT32_C(2166136261);
    for (size_t i = 0; i < sizeof(fake_bus.bmi_config); ++i) {
        config_checksum = (config_checksum ^ fake_bus.bmi_config[i]) * UINT32_C(16777619);
    }
    /* Independent fingerprint of Bosch's v2.86.1 base configuration. */
    assert(config_checksum == UINT32_C(0x95bb75b5));
    assert(fake_bus.config_completions == 1u && fake_bus.status_checked);
    assert(fake_bus.bmi_registers[0x40u] == 0xa8u); /* 100 Hz, normal filtering. */
    assert(fake_bus.bmi_registers[0x41u] == 0x01u); /* +/-4 g, 8192 LSB/g. */
    assert(fake_bus.bmi_registers[0x7du] == 0x04u); /* Accel only, after ready. */
}

static void test_bmi270_rejects_incomplete_initialization(void) {
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, NULL, NULL, fake_delay_ms};
    const whm_i2c_device_t bmi = {&hal, 0x68u};
    const uint8_t failed_statuses[] = {0u, 2u, 3u, 0x0fu};
    for (size_t i = 0; i < sizeof(failed_statuses); ++i) {
        fake_bus = (fake_i2c_bus_t){0};
        fake_bus.chip_id = 0x24u;
        fake_bus.force_internal_status = true;
        fake_bus.internal_status = failed_statuses[i];
        assert(!whm_bmi270_init(&bmi));
        assert(fake_bus.bmi_registers[0x7du] == 0u);
    }
    fake_bus = (fake_i2c_bus_t){0};
    fake_bus.chip_id = 0x24u;
    fake_bus.fail_config_after_bytes = 4096u;
    assert(!whm_bmi270_init(&bmi));
    assert(fake_bus.bmi_config_bytes == 4096u && fake_bus.config_completions == 0u);
    assert(fake_bus.bmi_registers[0x7du] == 0u);
    fake_bus = (fake_i2c_bus_t){0};
    fake_bus.chip_id = 0x24u;
    fake_bus.fail_status_read = true;
    assert(!whm_bmi270_init(&bmi));
    assert(fake_bus.bmi_registers[0x7du] == 0u);
    fake_bus = (fake_i2c_bus_t){0};
    assert(!whm_bmi270_init(&bmi)); /* Wrong chip ID. */
    assert(!whm_bmi270_init(NULL));
    assert(!whm_bmi270_init(&(whm_i2c_device_t){NULL, 0x68u}));
    whm_hal_t no_delay_hal = hal;
    no_delay_hal.delay_ms = NULL;
    fake_bus = (fake_i2c_bus_t){0};
    fake_bus.chip_id = 0x24u;
    assert(!whm_bmi270_init(&(whm_i2c_device_t){&no_delay_hal, 0x68u}));
    assert(fake_bus.writes == 0u);
}
int main(void) {
    test_ring_fifo_and_overflow();
    test_protocol_round_trip_and_crc();
    test_state_transitions();
    test_signal_estimator();
    test_runtime_output_cadence();
    test_runtime_suppresses_no_contact();
    test_acceleration_mg_rounding_and_limits();
    test_max30102_rejects_reserved_sample_averages();
    test_bmi270_uploads_configuration_before_enabling_acceleration();
    test_bmi270_rejects_incomplete_initialization();
    test_sensor_driver_register_contracts();
    test_controller_poll_path();
    puts("test_core: all tests passed");
    return 0;
}

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
} fake_i2c_bus_t;

static fake_i2c_bus_t fake_bus;
static uint64_t fake_time_us;
static unsigned fake_ble_count;
static bool fake_fifo_synthetic;
static size_t fake_fifo_index;

static bool fake_i2c_write(uint8_t address, const uint8_t *data, size_t length) {
    (void)address;
    assert(length >= 1u);
    fake_bus.selected_register = data[0];
    fake_bus.writes++;
    return true;
}

static bool fake_i2c_read(uint8_t address, uint8_t *data, size_t length) {
    (void)address;
    if (fake_bus.selected_register == 0x00u && length == 1u) {
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
    } else if (fake_bus.selected_register == 0x12u && length == sizeof(fake_bus.accel)) {
        for (size_t i = 0; i < length; ++i) data[i] = fake_bus.accel[i];
    } else if (fake_bus.selected_register == 0x00u && length == sizeof(fake_bus.temperature)) {
        for (size_t i = 0; i < length; ++i) data[i] = fake_bus.temperature[i];
    } else {
        return false;
    }
    return true;
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
    fake_bus.accel[4] = 0xe8u; fake_bus.accel[5] = 0x03u;
    fake_bus.temperature[0] = 0x0du; fake_bus.temperature[1] = 0x48u;
    fake_time_us = 0u;
    fake_ble_count = 0u;
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, fake_ble_send, fake_monotonic_time_us, NULL};
    whm_controller_t controller;
    assert(whm_controller_init(&controller, &hal,
                               (whm_i2c_device_t){&hal, 0x57u},
                               (whm_i2c_device_t){&hal, 0x68u},
                               (whm_i2c_device_t){&hal, 0x48u},
                               3800u));
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
    fake_bus.accel[0] = 0xe8u; fake_bus.accel[1] = 0x03u;
    fake_bus.accel[2] = 0x18u; fake_bus.accel[3] = 0xfcu;
    fake_bus.accel[4] = 0x00u; fake_bus.accel[5] = 0x04u;
    fake_bus.temperature[0] = 0x0du; fake_bus.temperature[1] = 0x48u;
    const whm_hal_t hal = {fake_i2c_write, fake_i2c_read, NULL, NULL, NULL};
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
    assert(x == 1000 && y == -1000 && z == 1024);
    assert(whm_tmp117_init(&tmp));
    float temperature = 0.0f;
    assert(whm_tmp117_read_temperature_c(&tmp, &temperature));
    assert(temperature > 26.5f && temperature < 26.7f);
    assert(fake_bus.writes > 10u);
}
int main(void) {
    test_ring_fifo_and_overflow();
    test_protocol_round_trip_and_crc();
    test_state_transitions();
    test_signal_estimator();
    test_runtime_output_cadence();
    test_runtime_suppresses_no_contact();
    test_sensor_driver_register_contracts();
    test_controller_poll_path();
    puts("test_core: all tests passed");
    return 0;
}

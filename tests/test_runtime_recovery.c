#include "whm_protocol.h"
#include "whm_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static whm_sample_t sample_at(uint64_t timestamp_us) {
    const double t = (double)timestamp_us / 1000000.0;
    whm_sample_t sample = {0};
    sample.timestamp_us = timestamp_us;
    sample.ppg_red = (int32_t)(100000.0 + 12000.0 * sin(6.283185307179586 * 1.25 * t));
    sample.ppg_ir = (int32_t)(100000.0 + 11000.0 * sin(6.283185307179586 * 1.25 * t));
    sample.accel_z = 1000;
    sample.temperature_c = 33.5f;
    sample.battery_mv = 3800;
    sample.flags = WHM_SAMPLE_CONTACT_OK;
    return sample;
}

static bool push(whm_runtime_t *runtime, whm_sample_t sample, whm_result_t *result) {
    uint8_t packet[WHM_PACKET_SIZE];
    size_t length = 0;
    if (!whm_runtime_push_sample(runtime, &sample, packet, sizeof(packet), &length)) {
        assert(length == 0);
        return false;
    }
    assert(whm_protocol_decode(packet, length, result));
    return true;
}

static void fill_valid_window(whm_runtime_t *runtime) {
    whm_result_t result = {0};
    for (size_t i = 0; i < WHM_WINDOW_SAMPLES; ++i) {
        const bool emitted = push(runtime, sample_at(i * 10000u), &result);
        if (i + 1u == WHM_WINDOW_SAMPLES) {
            assert(emitted && result.state == WHM_STATE_VALID);
        }
    }
}

static void test_gap_rebuilds_a_complete_window(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    fill_valid_window(&runtime);
    whm_result_t result = {0};
    const uint64_t next = 8200000u;
    assert(push(&runtime, sample_at(next), &result));
    assert(result.state != WHM_STATE_VALID && result.heart_rate_bpm_x10 == 0);
    assert((result.flags & (1u << 4)) != 0u);
    assert(whm_ring_count(&runtime.window) == 1u);
    for (size_t i = 1; i < WHM_WINDOW_SAMPLES; ++i) {
        const bool emitted = push(&runtime, sample_at(next + i * 10000u), &result);
        if (i + 1u < WHM_WINDOW_SAMPLES) {
            assert(!emitted || result.state != WHM_STATE_VALID);
        } else {
            assert(emitted && result.state == WHM_STATE_VALID);
        }
    }
}

static void test_fault_is_reported_immediately_and_cannot_hide_in_history(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    fill_valid_window(&runtime);
    whm_sample_t bad = sample_at(8000000u);
    bad.flags |= WHM_SAMPLE_SENSOR_FAULT;
    whm_result_t result = {0};
    assert(push(&runtime, bad, &result));
    assert(result.state == WHM_STATE_FAULT && result.heart_rate_bpm_x10 == 0);
    assert((result.flags & (1u << 3)) != 0u);
    assert(whm_ring_count(&runtime.window) == 0u);
    for (size_t i = 0; i < WHM_WINDOW_SAMPLES; ++i) {
        const bool emitted = push(&runtime, sample_at(8010000u + i * 10000u), &result);
        if (i + 1u < WHM_WINDOW_SAMPLES) {
            assert(!emitted || result.state != WHM_STATE_VALID);
        } else {
            assert(emitted && result.state == WHM_STATE_VALID);
        }
    }
}

static void test_clock_rewind_and_contact_loss_suppress_old_values(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    fill_valid_window(&runtime);
    whm_result_t result = {0};
    assert(push(&runtime, sample_at(0u), &result));
    assert(result.state != WHM_STATE_VALID && result.heart_rate_bpm_x10 == 0);
    assert(whm_ring_count(&runtime.window) == 1u);
    whm_runtime_init(&runtime);
    fill_valid_window(&runtime);
    whm_sample_t detached = sample_at(8000000u);
    detached.flags = 0u;
    assert(push(&runtime, detached, &result));
    assert(result.state == WHM_STATE_WAIT_CONTACT && result.heart_rate_bpm_x10 == 0);
    assert(whm_ring_count(&runtime.window) == 0u);
}

static void test_small_packet_buffer_does_not_consume_samples(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    uint8_t packet[2] = {0};
    size_t length = 123u;
    const whm_sample_t sample = sample_at(0u);
    assert(!whm_runtime_push_sample(&runtime, &sample, packet, sizeof(packet), &length));
    assert(length == 0u && whm_ring_count(&runtime.window) == 0u);
}

static void test_accumulated_clock_drift_is_not_a_valid_pulse(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    whm_result_t result = {0};
    bool emitted = false;
    for (size_t i = 0; i < WHM_WINDOW_SAMPLES; ++i) {
        emitted = push(&runtime, sample_at(i * 12000u), &result);
    }
    assert(emitted && result.state != WHM_STATE_VALID);
    assert((result.flags & (1u << 4)) != 0u);
    assert(whm_ring_count(&runtime.window) == 1u);
}

static void test_compensating_jitter_cannot_disguise_nonuniform_sampling(void) {
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    whm_result_t result = {0};
    uint64_t timestamp = 0u;
    bool emitted = false;
    for (size_t i = 0; i < WHM_WINDOW_SAMPLES; ++i) {
        if (i > 0u) {
            timestamp += i <= 80u || i > 720u ? 5000u : 11250u;
        }
        emitted = push(&runtime, sample_at(timestamp), &result);
    }
    assert(emitted && result.state != WHM_STATE_VALID);
    assert((result.flags & (1u << 4)) != 0u);
    assert(whm_ring_count(&runtime.window) == 1u);
}

int main(void) {
    test_gap_rebuilds_a_complete_window();
    test_fault_is_reported_immediately_and_cannot_hide_in_history();
    test_clock_rewind_and_contact_loss_suppress_old_values();
    test_small_packet_buffer_does_not_consume_samples();
    test_accumulated_clock_drift_is_not_a_valid_pulse();
    test_compensating_jitter_cannot_disguise_nonuniform_sampling();
    puts("test_runtime_recovery: all tests passed");
    return 0;
}

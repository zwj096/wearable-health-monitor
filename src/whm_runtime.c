#include "whm_runtime.h"

#include "whm_protocol.h"
#include "whm_signal.h"

#include <math.h>
#include <string.h>

static uint16_t clamp_u16_from_float(float value) {
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 65535.0f) {
        return 65535u;
    }
    return (uint16_t)lroundf(value);
}

static uint8_t quality_percent(float value) {
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return 100u;
    }
    return (uint8_t)lroundf(value * 100.0f);
}

void whm_runtime_init(whm_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    whm_ring_init(&runtime->window, WHM_WINDOW_SAMPLES);
    whm_state_init(&runtime->state);
}

bool whm_runtime_push_sample(whm_runtime_t *runtime, const whm_sample_t *sample, uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    if (runtime == NULL || sample == NULL || packet == NULL || packet_length == NULL) {
        return false;
    }
    *packet_length = 0u;
    if (!whm_ring_push(&runtime->window, sample)) {
        return false;
    }
    if (whm_ring_count(&runtime->window) < WHM_WINDOW_SAMPLES) {
        return false;
    }
    if (runtime->output_started && sample->timestamp_us < runtime->last_output_us + (uint64_t)WHM_OUTPUT_INTERVAL_MS * 1000u) {
        return false;
    }
    const size_t count = whm_ring_copy_ordered(&runtime->window, runtime->ordered, WHM_WINDOW_SAMPLES);
    if (count != WHM_WINDOW_SAMPLES) {
        return false;
    }
    whm_estimate_t estimate = {0};
    if (!whm_estimate_window(runtime->ordered, count, &estimate)) {
        return false;
    }
    const whm_sample_t *latest = &runtime->ordered[count - 1u];
    const whm_state_input_t state_input = {
        estimate.contact_score >= 0.5f,
        estimate.valid,
        (latest->flags & WHM_SAMPLE_CHARGING) != 0u,
        (latest->flags & WHM_SAMPLE_SENSOR_FAULT) != 0u
    };
    whm_state_step(&runtime->state, &state_input);
    whm_result_t result = {0};
    result.sequence = runtime->sequence++;
    result.timestamp_ms = (uint32_t)(latest->timestamp_us / 1000u);
    result.heart_rate_bpm_x10 = runtime->state.state == WHM_STATE_VALID ? clamp_u16_from_float(estimate.heart_rate_bpm * 10.0f) : 0u;
    result.temperature_c_x100 = (int16_t)lroundf(latest->temperature_c * 100.0f);
    result.motion_mg = clamp_u16_from_float(estimate.motion_rms_mg);
    result.quality = quality_percent(estimate.signal_quality);
    result.state = (uint8_t)runtime->state.state;
    result.battery_mv = latest->battery_mv;
    result.flags = 0u;
    if (runtime->state.state == WHM_STATE_VALID) {
        result.flags |= 1u << 0;
    }
    if (estimate.contact_score < 0.5f) {
        result.flags |= 1u << 1;
    }
    if (state_input.charging) {
        result.flags |= 1u << 2;
    }
    if (state_input.sensor_fault) {
        result.flags |= 1u << 3;
    }
    const size_t encoded = whm_protocol_encode(&result, packet, packet_capacity);
    if (encoded == 0u) {
        return false;
    }
    runtime->last_output_us = latest->timestamp_us;
    runtime->output_started = true;
    *packet_length = encoded;
    return true;
}

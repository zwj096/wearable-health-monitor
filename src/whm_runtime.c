#include "whm_runtime.h"

#include "whm_protocol.h"
#include "whm_signal.h"

#include <math.h>
#include <string.h>

static uint16_t clamp_u16_from_float(float value) {
    if (!isfinite(value) || value <= 0.0f) {
        return 0u;
    }
    if (value >= 65535.0f) {
        return 65535u;
    }
    return (uint16_t)lroundf(value);
}

static uint8_t quality_percent(float value) {
    if (!isfinite(value) || value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return 100u;
    }
    return (uint8_t)lroundf(value * 100.0f);
}

static void restart_window(whm_runtime_t *runtime, whm_state_t state) {
    runtime->window.head = 0u;
    runtime->window.count = 0u;
    runtime->window.overflow_count = 0u;
    whm_state_init(&runtime->state);
    runtime->state.state = state;
}

static bool output_due(const whm_runtime_t *runtime, uint64_t now_us) {
    return !runtime->output_started || now_us < runtime->last_output_us ||
           now_us - runtime->last_output_us >= (uint64_t)WHM_OUTPUT_INTERVAL_MS * 1000u;
}

static bool emit_result(whm_runtime_t *runtime, const whm_sample_t *sample,
                        const whm_estimate_t *estimate, uint8_t extra_flags,
                        uint8_t *packet, size_t capacity, size_t *length) {
    whm_result_t result = {0};
    result.sequence = runtime->sequence;
    result.timestamp_ms = (uint32_t)(sample->timestamp_us / 1000u);
    result.state = (uint8_t)runtime->state.state;
    result.battery_mv = sample->battery_mv;
    result.flags = extra_flags;
    if (isfinite(sample->temperature_c)) {
        const float bounded = fmaxf(-327.68f, fminf(327.67f, sample->temperature_c));
        result.temperature_c_x100 = (int16_t)lroundf(bounded * 100.0f);
    }
    if (estimate != NULL) {
        result.motion_mg = clamp_u16_from_float(estimate->motion_rms_mg);
        result.quality = quality_percent(estimate->signal_quality);
        if (runtime->state.state == WHM_STATE_VALID) {
            result.heart_rate_bpm_x10 = clamp_u16_from_float(estimate->heart_rate_bpm * 10.0f);
            result.flags |= WHM_RESULT_VALID;
        }
    }
    if ((sample->flags & WHM_SAMPLE_CONTACT_OK) == 0u) {
        result.flags |= WHM_RESULT_NO_CONTACT;
    }
    if (runtime->state.state == WHM_STATE_CHARGING) {
        result.flags |= WHM_RESULT_CHARGING;
    }
    if (runtime->state.state == WHM_STATE_FAULT) {
        result.flags |= WHM_RESULT_SENSOR_FAULT;
    }
    const size_t encoded = whm_protocol_encode(&result, packet, capacity);
    if (encoded == 0u) {
        return false;
    }
    runtime->sequence++;
    runtime->last_output_us = sample->timestamp_us;
    runtime->output_started = true;
    *length = encoded;
    return true;
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
    if (packet_length != NULL) {
        *packet_length = 0u;
    }
    if (runtime == NULL || sample == NULL || packet == NULL || packet_length == NULL || packet_capacity < WHM_PACKET_SIZE) {
        return false;
    }
    const uint64_t period_us = 1000000u / WHM_SAMPLE_RATE_HZ;
    bool discontinuity = false;
    if (runtime->sample_started) {
        discontinuity = sample->timestamp_us <= runtime->last_sample_us;
        if (!discontinuity) {
            const uint64_t elapsed = sample->timestamp_us - runtime->last_sample_us;
            discontinuity = elapsed < period_us / 2u || elapsed > period_us + period_us / 2u;
        }
    }
    runtime->last_sample_us = sample->timestamp_us;
    runtime->sample_started = true;

    whm_state_t blocked_state = WHM_STATE_INIT;
    if ((sample->flags & WHM_SAMPLE_SENSOR_FAULT) != 0u || !isfinite(sample->temperature_c)) {
        blocked_state = WHM_STATE_FAULT;
    } else if ((sample->flags & WHM_SAMPLE_CHARGING) != 0u) {
        blocked_state = WHM_STATE_CHARGING;
    } else if ((sample->flags & WHM_SAMPLE_CONTACT_OK) == 0u) {
        blocked_state = WHM_STATE_WAIT_CONTACT;
    }
    if (blocked_state != WHM_STATE_INIT) {
        const bool changed = runtime->state.state != blocked_state;
        restart_window(runtime, blocked_state);
        if (changed || discontinuity || output_due(runtime, sample->timestamp_us)) {
            return emit_result(runtime, sample, NULL, discontinuity ? WHM_RESULT_DATA_GAP : 0u,
                               packet, packet_capacity, packet_length);
        }
        return false;
    }

    const bool recovering = runtime->state.state == WHM_STATE_FAULT ||
                            runtime->state.state == WHM_STATE_CHARGING ||
                            runtime->state.state == WHM_STATE_WAIT_CONTACT;
    if (discontinuity || recovering) {
        restart_window(runtime, WHM_STATE_BASELINE);
    }
    if (!whm_ring_push(&runtime->window, sample)) {
        return false;
    }
    if (discontinuity) {
        return emit_result(runtime, sample, NULL, WHM_RESULT_DATA_GAP,
                           packet, packet_capacity, packet_length);
    }
    if (whm_ring_count(&runtime->window) < WHM_WINDOW_SAMPLES) {
        return false;
    }
    if (!output_due(runtime, sample->timestamp_us)) {
        return false;
    }
    const size_t count = whm_ring_copy_ordered(&runtime->window, runtime->ordered, WHM_WINDOW_SAMPLES);
    if (count != WHM_WINDOW_SAMPLES) {
        return false;
    }
    const uint64_t expected_span = (uint64_t)(count - 1u) * period_us;
    const uint64_t actual_span = sample->timestamp_us - runtime->ordered[0].timestamp_us;
    const uint64_t span_error = actual_span > expected_span ? actual_span - expected_span : expected_span - actual_span;
    bool uniform_grid = true;
    for (size_t i = 1u; i < count; ++i) {
        const uint64_t observed = runtime->ordered[i].timestamp_us - runtime->ordered[0].timestamp_us;
        const uint64_t expected = (uint64_t)i * period_us;
        const uint64_t error = observed > expected ? observed - expected : expected - observed;
        if (error > period_us / 4u) {
            uniform_grid = false;
            break;
        }
    }
    if (!uniform_grid || span_error > expected_span / 50u) {
        restart_window(runtime, WHM_STATE_BASELINE);
        (void)whm_ring_push(&runtime->window, sample);
        return emit_result(runtime, sample, NULL, WHM_RESULT_DATA_GAP,
                           packet, packet_capacity, packet_length);
    }
    whm_estimate_t estimate = {0};
    if (!whm_estimate_window(runtime->ordered, count, &estimate)) {
        return false;
    }
    const whm_state_input_t state_input = {
        estimate.contact_score >= 0.5f,
        estimate.valid,
        false,
        false
    };
    whm_state_step(&runtime->state, &state_input);
    return emit_result(runtime, sample, &estimate, 0u, packet, packet_capacity, packet_length);
}

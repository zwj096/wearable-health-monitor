#include "whm_signal.h"

#include <math.h>
#include <stddef.h>

#define WHM_PI 3.14159265358979323846f
#define WHM_EPSILON 1.0e-6f

static float clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float channel_mean(const whm_sample_t *samples, size_t count, bool ir) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += ir ? samples[i].ppg_ir : samples[i].ppg_red;
    }
    return (float)(sum / (double)count);
}

static float channel_ac_rms(const whm_sample_t *samples, size_t count, float mean, bool ir) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float value = (float)(ir ? samples[i].ppg_ir : samples[i].ppg_red) - mean;
        sum += (double)value * (double)value;
    }
    return (float)sqrt(sum / (double)count);
}

static float channel_frequency(const whm_sample_t *samples, size_t count, float mean, bool ir) {
    float best_frequency = 0.0f;
    float best_power = 0.0f;
    const float resolution = (float)WHM_SAMPLE_RATE_HZ / (float)count;
    for (size_t bin = 4u; bin <= (size_t)(4.0f / resolution); ++bin) {
        const float frequency = (float)bin * resolution;
        if (frequency > 4.0f + WHM_EPSILON) {
            break;
        }
        double real = 0.0;
        double imag = 0.0;
        for (size_t n = 0; n < count; ++n) {
            const float sample = (float)(ir ? samples[n].ppg_ir : samples[n].ppg_red) - mean;
            const float phase = 2.0f * WHM_PI * frequency * (float)n / (float)WHM_SAMPLE_RATE_HZ;
            real += (double)sample * cosf(phase);
            imag -= (double)sample * sinf(phase);
        }
        const float power = (float)((real * real + imag * imag) / (double)count);
        if (power > best_power) {
            best_power = power;
            best_frequency = frequency;
        }
    }
    return best_frequency;
}

static float motion_rms(const whm_sample_t *samples, size_t count) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float x = (float)samples[i].accel_x;
        const float y = (float)samples[i].accel_y;
        const float z = (float)samples[i].accel_z;
        const float magnitude = sqrtf(x * x + y * y + z * z);
        const float dynamic = magnitude - 1000.0f;
        sum += (double)dynamic * (double)dynamic;
    }
    return (float)sqrt(sum / (double)count);
}

bool whm_estimate_window(const whm_sample_t *samples, size_t count, whm_estimate_t *out) {
    if (samples == NULL || out == NULL || count < 64u) {
        return false;
    }
    *out = (whm_estimate_t){0};
    size_t contact_count = 0u;
    for (size_t i = 0; i < count; ++i) {
        if ((samples[i].flags & WHM_SAMPLE_CONTACT_OK) != 0u) {
            contact_count++;
        }
    }
    out->contact_score = (float)contact_count / (float)count;
    const float red_mean = channel_mean(samples, count, false);
    const float ir_mean = channel_mean(samples, count, true);
    const float red_ac = channel_ac_rms(samples, count, red_mean, false);
    const float ir_ac = channel_ac_rms(samples, count, ir_mean, true);
    const float red_frequency = channel_frequency(samples, count, red_mean, false);
    const float ir_frequency = channel_frequency(samples, count, ir_mean, true);
    const float frequency = red_frequency > 0.0f ? red_frequency : ir_frequency;
    out->heart_rate_bpm = frequency * 60.0f;
    out->motion_rms_mg = motion_rms(samples, count);
    const float signal_strength = clamp01(fmaxf(red_ac, ir_ac) / 4000.0f);
    const float motion_score = 1.0f - clamp01(out->motion_rms_mg / 800.0f);
    out->signal_quality = clamp01(0.55f * signal_strength + 0.30f * out->contact_score + 0.15f * motion_score);
    out->valid = out->contact_score >= 0.5f && frequency >= 0.5f && frequency <= 4.0f && out->signal_quality >= 0.55f;
    return true;
}

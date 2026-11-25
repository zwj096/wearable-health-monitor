#include "whm_signal.h"

#include <math.h>
#include <stddef.h>

#define WHM_PI 3.14159265358979323846
#define WHM_MIN_FREQUENCY_HZ 0.5f
#define WHM_MAX_FREQUENCY_HZ 4.0f
#define WHM_MIN_CHANNEL_CONFIDENCE 0.55f
#define WHM_MAX_CHANNEL_DIFFERENCE_HZ 0.10f
#define WHM_SPECTRUM_BINS (4u * WHM_WINDOW_SAMPLES / WHM_SAMPLE_RATE_HZ + 2u)

typedef struct {
    float frequency_hz;
    float confidence;
} channel_estimate_t;

static float clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static double channel_value(const whm_sample_t *sample, bool ir) {
    return ir ? sample->ppg_ir : sample->ppg_red;
}

static double bin_power(const float *values, size_t count, size_t bin) {
    /* Goertzel needs one cosine per bin rather than sine/cosine per sample. */
    const double coefficient = 2.0 * cos(2.0 * WHM_PI * (double)bin / (double)count);
    double previous = 0.0;
    double previous_previous = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double current = values[i] + coefficient * previous - previous_previous;
        previous_previous = previous;
        previous = current;
    }
    return fmax(0.0, previous * previous + previous_previous * previous_previous -
                     coefficient * previous * previous_previous);
}

static channel_estimate_t estimate_channel(const whm_sample_t *samples, size_t count, bool ir) {
    channel_estimate_t result = {0};
    float windowed[WHM_WINDOW_SAMPLES];
    double powers[WHM_SPECTRUM_BINS] = {0};
    const double midpoint = ((double)count - 1.0) * 0.5;
    double sum = 0.0;
    double trend_sum = 0.0;
    for (size_t i = 0u; i < count; ++i) {
        const double value = channel_value(&samples[i], ir);
        sum += value;
        trend_sum += ((double)i - midpoint) * value;
    }
    const double mean = sum / (double)count;
    const double trend_denominator = (double)count * ((double)count * (double)count - 1.0) / 12.0;
    const double slope = trend_sum / trend_denominator;
    double ac_energy = 0.0;
    double windowed_sum = 0.0;
    for (size_t i = 0u; i < count; ++i) {
        const double detrended = channel_value(&samples[i], ir) - mean - slope * ((double)i - midpoint);
        const double hann = 0.5 - 0.5 * cos(2.0 * WHM_PI * (double)i / ((double)count - 1.0));
        ac_energy += detrended * detrended;
        windowed[i] = (float)(detrended * hann);
        windowed_sum += windowed[i];
    }
    double windowed_energy = 0.0;
    const float windowed_mean = (float)(windowed_sum / (double)count);
    for (size_t i = 0u; i < count; ++i) {
        windowed[i] -= windowed_mean;
        windowed_energy += (double)windowed[i] * windowed[i];
    }
    if (windowed_energy <= 1.0e-6) {
        return result;
    }

    const float resolution = (float)WHM_SAMPLE_RATE_HZ / (float)count;
    const size_t first_bin = (size_t)ceilf(WHM_MIN_FREQUENCY_HZ / resolution);
    const size_t last_bin = (size_t)floorf(WHM_MAX_FREQUENCY_HZ / resolution);
    const size_t first_neighbor = first_bin > 1u ? first_bin - 1u : 1u;
    size_t peak_bin = first_bin;
    for (size_t bin = first_neighbor; bin <= last_bin + 1u; ++bin) {
        powers[bin] = bin_power(windowed, count, bin);
        if (bin >= first_bin && bin <= last_bin && powers[bin] > powers[peak_bin]) {
            peak_bin = bin;
        }
    }
    if (powers[peak_bin] <= 1.0e-6) {
        return result;
    }

    /* A Hann peak spans three bins. Compare its energy with all-frequency
       energy (Parseval), so large broadband noise cannot earn quality by RMS. */
    const double peak_energy = powers[peak_bin - 1u] + powers[peak_bin] + powers[peak_bin + 1u];
    const float concentration = clamp01((float)(peak_energy / ((double)count * windowed_energy * 0.5)));
    const float strength = clamp01((float)sqrt(ac_energy / (double)count) / 4000.0f);
    result.confidence = strength * clamp01((concentration - 0.35f) / 0.50f);

    double offset = 0.0;
    if (peak_bin > 1u) {
        const double left = log(fmax(powers[peak_bin - 1u], 1.0e-12));
        const double center = log(powers[peak_bin]);
        const double right = log(fmax(powers[peak_bin + 1u], 1.0e-12));
        const double denominator = left - 2.0 * center + right;
        if (denominator < -1.0e-9) {
            offset = fmax(-0.5, fmin(0.5, 0.5 * (left - right) / denominator));
        }
    }
    result.frequency_hz = (float)(((double)peak_bin + offset) * resolution);
    if (result.frequency_hz < WHM_MIN_FREQUENCY_HZ || result.frequency_hz > WHM_MAX_FREQUENCY_HZ) {
        result.confidence = 0.0f;
    }
    return result;
}

static float motion_rms(const whm_sample_t *samples, size_t count) {
    double magnitude_energy = 0.0;
    double sums[3] = {0};
    double squared_sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        /* whm_sample_t acceleration is in mg, including static gravity. */
        const double x = samples[i].accel_x;
        const double y = samples[i].accel_y;
        const double z = samples[i].accel_z;
        const double squared_magnitude = x * x + y * y + z * z;
        const double dynamic = sqrt(squared_magnitude) - 1000.0;
        magnitude_energy += dynamic * dynamic;
        sums[0] += x;
        sums[1] += y;
        sums[2] += z;
        squared_sum += squared_magnitude;
    }
    const double mean_squared = (sums[0] * sums[0] + sums[1] * sums[1] + sums[2] * sums[2]) /
                                ((double)count * (double)count);
    const double vector_variance = fmax(0.0, squared_sum / (double)count - mean_squared);
    /* Axis variance also catches motion perpendicular to gravity, which
       scarcely changes |a|-1g at small amplitudes. */
    return (float)sqrt(fmax(vector_variance, magnitude_energy / (double)count));
}

bool whm_estimate_window(const whm_sample_t *samples, size_t count, whm_estimate_t *out) {
    if (out == NULL) {
        return false;
    }
    *out = (whm_estimate_t){0};
    if (samples == NULL || count < 64u || count > WHM_WINDOW_SAMPLES) {
        return false;
    }
    size_t contact_count = 0u;
    bool invalid_sample = false;
    for (size_t i = 0; i < count; ++i) {
        if ((samples[i].flags & WHM_SAMPLE_CONTACT_OK) != 0u) {
            contact_count++;
        }
        if ((samples[i].flags & (WHM_SAMPLE_SENSOR_FAULT | WHM_SAMPLE_CHARGING)) != 0u) {
            invalid_sample = true;
        }
    }
    out->contact_score = (float)contact_count / (float)count;
    out->motion_rms_mg = motion_rms(samples, count);
    if (invalid_sample || out->contact_score < 0.5f) {
        return true;
    }
    const channel_estimate_t red = estimate_channel(samples, count, false);
    const channel_estimate_t ir = estimate_channel(samples, count, true);
    const bool red_ok = red.confidence >= WHM_MIN_CHANNEL_CONFIDENCE;
    const bool ir_ok = ir.confidence >= WHM_MIN_CHANNEL_CONFIDENCE;
    float frequency = 0.0f;
    float confidence = 0.0f;
    if (red_ok && ir_ok) {
        if (fabsf(red.frequency_hz - ir.frequency_hz) > WHM_MAX_CHANNEL_DIFFERENCE_HZ) {
            return true;
        }
        confidence = 0.5f * (red.confidence + ir.confidence);
        frequency = (red.frequency_hz * red.confidence + ir.frequency_hz * ir.confidence) /
                    (red.confidence + ir.confidence);
    } else if (red_ok || ir_ok) {
        const channel_estimate_t selected = red_ok ? red : ir;
        frequency = selected.frequency_hz;
        confidence = 0.85f * selected.confidence;
    }
    /* These are conservative engineering heuristics, not calibrated clinical
       scores. Motion can suppress validity even when optical RMS is large. */
    const float motion_score = 1.0f - clamp01(out->motion_rms_mg / 400.0f);
    out->signal_quality = clamp01(confidence * out->contact_score * motion_score);
    out->valid = frequency >= WHM_MIN_FREQUENCY_HZ && frequency <= WHM_MAX_FREQUENCY_HZ &&
                 out->signal_quality >= 0.55f;
    if (out->valid) {
        out->heart_rate_bpm = frequency * 60.0f;
    }
    return true;
}

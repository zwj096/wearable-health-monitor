#include "whm_signal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846

static whm_sample_t samples[WHM_WINDOW_SAMPLES + 1u];
static unsigned failures;
static unsigned checks;

static void check(bool condition, const char *description) {
    checks++;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static void make_pulses(double red_bpm, double ir_bpm) {
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES + 1u; ++i) {
        const double time_s = (double)i / WHM_SAMPLE_RATE_HZ;
        samples[i] = (whm_sample_t){0};
        samples[i].timestamp_us = (uint64_t)i * 10000u;
        samples[i].ppg_red = (int32_t)(100000.0 + 12000.0 * sin(2.0 * TEST_PI * red_bpm / 60.0 * time_s));
        samples[i].ppg_ir = (int32_t)(100000.0 + 11000.0 * sin(2.0 * TEST_PI * ir_bpm / 60.0 * time_s + 0.4));
        samples[i].accel_z = 1000;
        samples[i].flags = WHM_SAMPLE_CONTACT_OK;
    }
}

static int32_t noise(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return (int32_t)((*state >> 8u) % 60001u) - 30000;
}

static whm_estimate_t estimate(void) {
    whm_estimate_t result = {0};
    check(whm_estimate_window(samples, WHM_WINDOW_SAMPLES, &result), "accept a full input window");
    return result;
}

static void expect_rejected(whm_estimate_t result, const char *description) {
    printf("rejection %-25s valid=%u quality=%.3f motion=%.1f mg\n",
           description, result.valid ? 1u : 0u,
           (double)result.signal_quality, (double)result.motion_rms_mg);
    check(!result.valid && result.signal_quality < 0.55f, description);
}

static void test_clean_rates_and_drift(void) {
    const double rates[] = {71.0, 75.0, 83.0, 101.0, 143.0};
    for (size_t rate = 0u; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        make_pulses(rates[rate], rates[rate]);
        const whm_estimate_t result = estimate();
        printf("clean expected=%.1f estimated=%.3f valid=%u quality=%.3f\n",
               rates[rate], (double)result.heart_rate_bpm,
               result.valid ? 1u : 0u, (double)result.signal_quality);
        check(result.valid, "clean pulse remains valid");
        check(fabs((double)result.heart_rate_bpm - rates[rate]) < 2.0,
              "clean pulse error stays below 2 bpm between DFT bins");
    }
    make_pulses(71.0, 71.0);
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        samples[i].ppg_red += (int32_t)(i * 80u);
        samples[i].ppg_ir -= (int32_t)(i * 70u);
    }
    const whm_estimate_t result = estimate();
    check(result.valid && fabs((double)result.heart_rate_bpm - 71.0) < 2.0,
          "linear optical drift does not move the pulse peak");
}

static void test_channel_disagreement_and_fallback(void) {
    make_pulses(75.0, 120.0);
    expect_rejected(estimate(), "conflicting clean channels");

    make_pulses(83.0, 83.0);
    uint32_t seed = 0x12345678u;
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        samples[i].ppg_red = 100000 + noise(&seed);
    }
    whm_estimate_t result = estimate();
    check(result.valid && fabs((double)result.heart_rate_bpm - 83.0) < 2.0,
          "clean IR channel survives a noisy red channel");

    make_pulses(101.0, 101.0);
    seed = 0x76543210u;
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        samples[i].ppg_ir = 100000 + noise(&seed);
    }
    result = estimate();
    check(result.valid && fabs((double)result.heart_rate_bpm - 101.0) < 2.0,
          "clean red channel survives a noisy IR channel");

    make_pulses(75.0, 75.0);
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        samples[i].ppg_red = 100000;
    }
    result = estimate();
    check(result.valid && fabs((double)result.heart_rate_bpm - 75.0) < 2.0,
          "clean IR channel survives a flat red channel");
}

static void test_noise_and_motion_rejection(void) {
    make_pulses(75.0, 75.0);
    uint32_t red_seed = 0x9abcdef0u;
    uint32_t ir_seed = 0x13579bdfu;
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        samples[i].ppg_red = 100000 + noise(&red_seed);
        samples[i].ppg_ir = 100000 + noise(&ir_seed);
    }
    expect_rejected(estimate(), "high amplitude broadband noise");

    make_pulses(75.0, 75.0);
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        const double time_s = (double)i / WHM_SAMPLE_RATE_HZ;
        samples[i].accel_z = (int16_t)(1000.0 + 900.0 * sin(2.0 * TEST_PI * 2.0 * time_s));
    }
    expect_rejected(estimate(), "large axial motion");

    make_pulses(75.0, 75.0);
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        const double time_s = (double)i / WHM_SAMPLE_RATE_HZ;
        samples[i].accel_x = (int16_t)(280.0 * sin(2.0 * TEST_PI * 2.0 * time_s));
    }
    const whm_estimate_t result = estimate();
    expect_rejected(result, "transverse motion");
    check(result.motion_rms_mg > 190.0f && result.motion_rms_mg < 205.0f,
          "transverse acceleration is measured without gravity masking it");
}

static void test_contact_fault_and_bounds(void) {
    make_pulses(75.0, 75.0);
    for (size_t i = 0u; i < WHM_WINDOW_SAMPLES; ++i) {
        samples[i].flags = 0u;
    }
    expect_rejected(estimate(), "no contact");

    make_pulses(75.0, 75.0);
    samples[100].flags |= WHM_SAMPLE_SENSOR_FAULT;
    expect_rejected(estimate(), "historical sensor fault");

    make_pulses(75.0, 75.0);
    samples[200].flags |= WHM_SAMPLE_CHARGING;
    expect_rejected(estimate(), "charging sample in window");

    whm_estimate_t result = {0};
    check(!whm_estimate_window(samples, WHM_WINDOW_SAMPLES + 1u, &result),
          "reject input larger than the bounded working buffer");
    check(!whm_estimate_window(samples, 63u, &result), "reject undersized input");
    check(!whm_estimate_window(NULL, WHM_WINDOW_SAMPLES, &result), "reject null input");
    check(!whm_estimate_window(samples, WHM_WINDOW_SAMPLES, NULL), "reject null output");
}

static void test_frequency_band_edges(void) {
    const double rates[] = {30.0, 240.0};
    for (size_t rate = 0u; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        make_pulses(rates[rate], rates[rate]);
        const whm_estimate_t result = estimate();
        printf("band edge expected=%.1f estimated=%.3f valid=%u\n",
               rates[rate], (double)result.heart_rate_bpm, result.valid ? 1u : 0u);
        check(result.valid && fabs((double)result.heart_rate_bpm - rates[rate]) < 2.0,
              "clean pulse remains valid at both supported frequency edges");
    }
}

int main(void) {
    test_clean_rates_and_drift();
    test_channel_disagreement_and_fallback();
    test_noise_and_motion_rejection();
    test_contact_fault_and_bounds();
    test_frequency_band_edges();
    printf("test_signal: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? 0 : 1;
}

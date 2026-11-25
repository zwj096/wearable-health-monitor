#include "whm_protocol.h"
#include "whm_runtime.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WHM_PI 3.14159265358979323846

typedef enum {
    MODE_NOMINAL,
    MODE_MOTION,
    MODE_NO_CONTACT
} simulation_mode_t;

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s [--seconds N] [--mode nominal|motion|nocontact]\n", program);
}

static bool parse_args(int argc, char **argv, unsigned *seconds, simulation_mode_t *mode) {
    *seconds = 12u;
    *mode = MODE_NOMINAL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            char *end = NULL;
            const unsigned long parsed = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || parsed == 0ul || parsed > 3600ul) {
                return false;
            }
            *seconds = (unsigned)parsed;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "nominal") == 0) {
                *mode = MODE_NOMINAL;
            } else if (strcmp(value, "motion") == 0) {
                *mode = MODE_MOTION;
            } else if (strcmp(value, "nocontact") == 0) {
                *mode = MODE_NO_CONTACT;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

static whm_sample_t synthesize_sample(size_t index, simulation_mode_t mode) {
    const double time_s = (double)index / WHM_SAMPLE_RATE_HZ;
    const bool contact = mode != MODE_NO_CONTACT;
    const bool motion = mode == MODE_MOTION && time_s >= 4.0 && time_s < 8.0;
    const double pulse = sin(2.0 * WHM_PI * 1.25 * time_s);
    const double motion_component = motion ? 18000.0 * sin(2.0 * WHM_PI * 2.0 * time_s) : 0.0;
    whm_sample_t sample = {0};
    sample.timestamp_us = (uint64_t)(time_s * 1000000.0);
    sample.ppg_red = (int32_t)(100000.0 + (contact ? 12000.0 * pulse : 300.0 * pulse) + motion_component);
    sample.ppg_ir = (int32_t)(100000.0 + (contact ? 11000.0 * pulse : 250.0 * pulse) + motion_component * 0.9);
    sample.accel_x = motion ? (int16_t)(280.0 * sin(2.0 * WHM_PI * 2.0 * time_s)) : 0;
    sample.accel_y = 0;
    sample.accel_z = 1000;
    const double temperature_time_s = floor(time_s);
    sample.temperature_c = (float)(33.3 + 0.2 * (1.0 - exp(-temperature_time_s / 20.0)));
    sample.battery_mv = (uint16_t)(3800u - (unsigned)(index / (WHM_SAMPLE_RATE_HZ * 60u)));
    sample.flags = contact ? WHM_SAMPLE_CONTACT_OK : 0u;
    return sample;
}

int main(int argc, char **argv) {
    unsigned seconds = 0u;
    simulation_mode_t mode = MODE_NOMINAL;
    if (!parse_args(argc, argv, &seconds, &mode)) {
        usage(argv[0]);
        return 2;
    }
    whm_runtime_t runtime;
    whm_runtime_init(&runtime);
    uint8_t packet[WHM_PACKET_SIZE] = {0};
    size_t packet_length = 0u;
    printf("mode,sequence,timestamp_ms,state,heart_rate_bpm,temperature_c,quality,motion_mg,battery_mv,flags\n");
    const size_t total_samples = (size_t)seconds * WHM_SAMPLE_RATE_HZ;
    for (size_t index = 0; index < total_samples; ++index) {
        const whm_sample_t sample = synthesize_sample(index, mode);
        if (!whm_runtime_push_sample(&runtime, &sample, packet, sizeof(packet), &packet_length)) {
            continue;
        }
        whm_result_t result = {0};
        if (!whm_protocol_decode(packet, packet_length, &result)) {
            fprintf(stderr, "protocol decode failed at sample %zu\n", index);
            return 1;
        }
        printf("%s,%u,%u,%u,%.1f,%.2f,%u,%u,%u,%u\n",
               mode == MODE_NOMINAL ? "nominal" : mode == MODE_MOTION ? "motion" : "nocontact",
               result.sequence,
               result.timestamp_ms,
               result.state,
               (double)result.heart_rate_bpm_x10 / 10.0,
               (double)result.temperature_c_x100 / 100.0,
               result.quality,
               result.motion_mg,
               result.battery_mv,
               result.flags);
    }
    return 0;
}

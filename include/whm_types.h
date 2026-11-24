#ifndef WHM_TYPES_H
#define WHM_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WHM_SAMPLE_RATE_HZ 100U
#define WHM_TEMP_RATE_HZ 1U
#define WHM_WINDOW_SECONDS 8U
#define WHM_WINDOW_SAMPLES (WHM_SAMPLE_RATE_HZ * WHM_WINDOW_SECONDS)
#define WHM_OUTPUT_INTERVAL_MS 2000U
#define WHM_RING_MAX_CAPACITY 1024U

enum {
    WHM_SAMPLE_CONTACT_OK = 1u << 0,
    WHM_SAMPLE_CHARGING = 1u << 1,
    WHM_SAMPLE_SENSOR_FAULT = 1u << 2
};

typedef enum {
    WHM_STATE_INIT = 0,
    WHM_STATE_WAIT_CONTACT = 1,
    WHM_STATE_BASELINE = 2,
    WHM_STATE_VALID = 3,
    WHM_STATE_LOW_QUALITY = 4,
    WHM_STATE_CHARGING = 5,
    WHM_STATE_FAULT = 6
} whm_state_t;

typedef struct {
    uint64_t timestamp_us;
    int32_t ppg_red;
    int32_t ppg_ir;
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    float temperature_c;
    uint16_t battery_mv;
    uint8_t flags;
} whm_sample_t;

typedef struct {
    float heart_rate_bpm;
    float motion_rms_mg;
    float contact_score;
    float signal_quality;
    bool valid;
} whm_estimate_t;

typedef struct {
    uint16_t sequence;
    uint32_t timestamp_ms;
    uint16_t heart_rate_bpm_x10;
    int16_t temperature_c_x100;
    uint16_t motion_mg;
    uint8_t quality;
    uint8_t state;
    uint16_t battery_mv;
    uint8_t flags;
} whm_result_t;

#endif

#ifndef WHM_TEST_SUPPORT_H
#define WHM_TEST_SUPPORT_H

#include <math.h>
#include <stdint.h>

#define WHM_TEST_PI 3.14159265358979323846

static inline int32_t whm_test_ppg(double time_s, double heart_rate_hz) {
    return (int32_t)(100000.0 + 12000.0 * sin(2.0 * WHM_TEST_PI * heart_rate_hz * time_s));
}

#endif

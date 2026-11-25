#ifndef WHM_SENSOR_DRIVERS_H
#define WHM_SENSOR_DRIVERS_H

#include "whm_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const whm_hal_t *hal;
    uint8_t address;
} whm_i2c_device_t;

typedef struct {
    uint8_t sample_average; /* FIFO_SMP_AVE encoding 0..5 (1..32 samples). */
    uint8_t led_current_code;
} whm_max30102_config_t;

bool whm_max30102_init(const whm_i2c_device_t *device, const whm_max30102_config_t *config);
bool whm_max30102_read_fifo(const whm_i2c_device_t *device, int32_t *red, int32_t *ir);

/* Reset, upload the bundled Bosch configuration and verify INTERNAL_STATUS,
 * then enable 100 Hz acceleration at +/-4 g. Requires HAL delay_ms. */
bool whm_bmi270_init(const whm_i2c_device_t *device);
/* Read acceleration in mg (1000 mg = 1 g), using the initialized +/-4 g range.
 * Conversion rounds to nearest mg, with halfway cases away from zero. */
bool whm_bmi270_read_accel(const whm_i2c_device_t *device, int16_t *x_mg, int16_t *y_mg, int16_t *z_mg);

bool whm_tmp117_init(const whm_i2c_device_t *device);
bool whm_tmp117_read_temperature_c(const whm_i2c_device_t *device, float *temperature_c);

#endif

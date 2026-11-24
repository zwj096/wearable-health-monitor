#ifndef WHM_CONTROLLER_H
#define WHM_CONTROLLER_H

#include "whm_runtime.h"
#include "whm_protocol.h"
#include "whm_sensor_drivers.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    whm_i2c_device_t max30102;
    whm_i2c_device_t bmi270;
    whm_i2c_device_t tmp117;
    const whm_hal_t *hal;
    whm_runtime_t runtime;
    uint16_t battery_mv;
    float latest_temperature_c;
    uint32_t sample_index;
    bool initialized;
    uint8_t last_packet[WHM_PACKET_SIZE];
    size_t last_packet_length;
} whm_controller_t;

bool whm_controller_init(whm_controller_t *controller,
                         const whm_hal_t *hal,
                         whm_i2c_device_t max30102,
                         whm_i2c_device_t bmi270,
                         whm_i2c_device_t tmp117,
                         uint16_t initial_battery_mv);
void whm_controller_set_battery_mv(whm_controller_t *controller, uint16_t battery_mv);
bool whm_controller_poll(whm_controller_t *controller);

#endif

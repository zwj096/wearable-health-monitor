#include "whm_controller.h"

#include <string.h>

static uint64_t controller_time_us(const whm_controller_t *controller) {
    if (controller->hal != NULL && controller->hal->monotonic_time_us != NULL) {
        return controller->hal->monotonic_time_us();
    }
    return (uint64_t)controller->sample_index * 1000000u / WHM_SAMPLE_RATE_HZ;
}

bool whm_controller_init(whm_controller_t *controller,
                         const whm_hal_t *hal,
                         whm_i2c_device_t max30102,
                         whm_i2c_device_t bmi270,
                         whm_i2c_device_t tmp117,
                         uint16_t initial_battery_mv) {
    if (controller == NULL || hal == NULL) {
        return false;
    }
    memset(controller, 0, sizeof(*controller));
    controller->hal = hal;
    controller->max30102 = max30102;
    controller->bmi270 = bmi270;
    controller->tmp117 = tmp117;
    controller->battery_mv = initial_battery_mv;
    controller->latest_temperature_c = 0.0f;
    if (!whm_max30102_init(&controller->max30102, &(whm_max30102_config_t){0u, 0x19u})) { /* 100 Hz FIFO, 5 mA/channel. */
        return false;
    }
    if (!whm_bmi270_init(&controller->bmi270) || !whm_tmp117_init(&controller->tmp117)) {
        return false;
    }
    whm_runtime_init(&controller->runtime);
    controller->initialized = true;
    return true;
}

void whm_controller_set_battery_mv(whm_controller_t *controller, uint16_t battery_mv) {
    if (controller != NULL) {
        controller->battery_mv = battery_mv;
    }
}

bool whm_controller_poll(whm_controller_t *controller) {
    if (controller == NULL || !controller->initialized) {
        return false;
    }
    int32_t red = 0;
    int32_t ir = 0;
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    bool sensor_fault = false;
    if (!whm_max30102_read_fifo(&controller->max30102, &red, &ir)) {
        sensor_fault = true;
    }
    if (!whm_bmi270_read_accel(&controller->bmi270, &x, &y, &z)) {
        sensor_fault = true;
    }
    if (controller->sample_index % WHM_SAMPLE_RATE_HZ == 0u &&
        !whm_tmp117_read_temperature_c(&controller->tmp117, &controller->latest_temperature_c)) {
        sensor_fault = true;
    }
    whm_sample_t sample = {0};
    sample.timestamp_us = controller_time_us(controller);
    sample.ppg_red = red;
    sample.ppg_ir = ir;
    sample.accel_x = x;
    sample.accel_y = y;
    sample.accel_z = z;
    sample.temperature_c = controller->latest_temperature_c;
    sample.battery_mv = controller->battery_mv;
    if (!sensor_fault && red > 1000 && ir > 1000) {
        sample.flags |= WHM_SAMPLE_CONTACT_OK;
    }
    if (sensor_fault) {
        sample.flags |= WHM_SAMPLE_SENSOR_FAULT;
    }
    controller->sample_index++;
    uint8_t packet[WHM_PACKET_SIZE] = {0};
    size_t packet_length = 0u;
    const bool emitted = whm_runtime_push_sample(&controller->runtime,
                                                  &sample,
                                                  packet,
                                                  sizeof(packet),
                                                  &packet_length);
    if (emitted) {
        memcpy(controller->last_packet, packet, packet_length);
        controller->last_packet_length = packet_length;
    }
    if (emitted && controller->hal->ble_send != NULL &&
        !controller->hal->ble_send(packet, packet_length)) {
        return false;
    }
    return !sensor_fault;
}

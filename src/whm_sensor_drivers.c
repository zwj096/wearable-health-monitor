#include "whm_sensor_drivers.h"

#include <stddef.h>

static bool write_register(const whm_i2c_device_t *device, uint8_t reg, const uint8_t *values, size_t value_count) {
    if (device == NULL || device->hal == NULL || device->hal->i2c_write == NULL || value_count > 8u) {
        return false;
    }
    uint8_t buffer[9] = {0};
    buffer[0] = reg;
    for (size_t i = 0; i < value_count; ++i) {
        buffer[i + 1u] = values[i];
    }
    return device->hal->i2c_write(device->address, buffer, value_count + 1u);
}

static bool read_register(const whm_i2c_device_t *device, uint8_t reg, uint8_t *values, size_t value_count) {
    if (device == NULL || device->hal == NULL || device->hal->i2c_write == NULL || device->hal->i2c_read == NULL || values == NULL) {
        return false;
    }
    if (!device->hal->i2c_write(device->address, &reg, 1u)) {
        return false;
    }
    return device->hal->i2c_read(device->address, values, value_count);
}

static bool write_u8(const whm_i2c_device_t *device, uint8_t reg, uint8_t value) {
    return write_register(device, reg, &value, 1u);
}

bool whm_max30102_init(const whm_i2c_device_t *device, const whm_max30102_config_t *config) {
    if (device == NULL || config == NULL || config->sample_average > 7u) {
        return false;
    }
    const uint8_t reset = 0x40u;
    const uint8_t fifo_config = (uint8_t)((config->sample_average << 5u) | 0x0fu);
    const uint8_t mode = 0x03u;
    const uint8_t spo2_config = 0x47u; /* 4096 nA range, 100 Hz, 411 us pulse. */
    if (!write_register(device, 0x09u, &reset, 1u)) {
        return false;
    }
    if (device->hal->delay_ms != NULL) {
        device->hal->delay_ms(10u);
    }
    return write_u8(device, 0x04u, 0x00u) &&
           write_u8(device, 0x06u, 0x00u) &&
           write_u8(device, 0x08u, fifo_config) &&
           write_u8(device, 0x0au, spo2_config) &&
           write_u8(device, 0x0cu, config->led_current_code) &&
           write_u8(device, 0x0du, config->led_current_code) &&
           write_u8(device, 0x09u, mode);
}

bool whm_max30102_read_fifo(const whm_i2c_device_t *device, int32_t *red, int32_t *ir) {
    if (red == NULL || ir == NULL) {
        return false;
    }
    uint8_t bytes[6] = {0};
    if (!read_register(device, 0x07u, bytes, sizeof(bytes))) {
        return false;
    }
    *red = (int32_t)(((uint32_t)bytes[0] << 16u | (uint32_t)bytes[1] << 8u | bytes[2]) & 0x3ffffu);
    *ir = (int32_t)(((uint32_t)bytes[3] << 16u | (uint32_t)bytes[4] << 8u | bytes[5]) & 0x3ffffu);
    return true;
}

bool whm_bmi270_init(const whm_i2c_device_t *device) {
    uint8_t chip_id = 0u;
    if (!read_register(device, 0x00u, &chip_id, 1u) || chip_id != 0x24u) {
        return false;
    }
    if (!write_u8(device, 0x7eu, 0xb6u)) {
        return false;
    }
    if (device->hal->delay_ms != NULL) {
        device->hal->delay_ms(2u);
    }
    return write_u8(device, 0x7du, 0x04u) && /* accelerometer normal mode */
           write_u8(device, 0x40u, 0xa8u) && /* 100 Hz, normal bandwidth */
           write_u8(device, 0x41u, 0x00u);   /* +/-2 g */
}

bool whm_bmi270_read_accel(const whm_i2c_device_t *device, int16_t *x, int16_t *y, int16_t *z) {
    if (x == NULL || y == NULL || z == NULL) {
        return false;
    }
    uint8_t bytes[6] = {0};
    if (!read_register(device, 0x12u, bytes, sizeof(bytes))) {
        return false;
    }
    *x = (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
    *y = (int16_t)((uint16_t)bytes[2] | ((uint16_t)bytes[3] << 8u));
    *z = (int16_t)((uint16_t)bytes[4] | ((uint16_t)bytes[5] << 8u));
    return true;
}

bool whm_tmp117_init(const whm_i2c_device_t *device) {
    const uint8_t config[2] = {0x02u, 0x20u}; /* continuous conversion, default averaging. */
    return write_register(device, 0x01u, config, sizeof(config));
}

bool whm_tmp117_read_temperature_c(const whm_i2c_device_t *device, float *temperature_c) {
    if (temperature_c == NULL) {
        return false;
    }
    uint8_t bytes[2] = {0};
    if (!read_register(device, 0x00u, bytes, sizeof(bytes))) {
        return false;
    }
    const int16_t raw = (int16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
    *temperature_c = (float)raw * 0.0078125f;
    return true;
}

#include "whm_sensor_drivers.h"
#include "../vendor/bosch/bmi270_config.h"

#include <stddef.h>

_Static_assert(sizeof(whm_bmi270_config) == 8192u, "BMI270 requires the complete 8 KiB configuration");

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
    if (device == NULL || config == NULL || config->sample_average > 5u) {
        return false;
    }
    const uint8_t reset = 0x40u;
    const uint8_t fifo_config = (uint8_t)((config->sample_average << 5u) | 0x0fu);
    const uint8_t mode = 0x03u;
    const uint8_t spo2_config = 0x47u; /* 8192 nA range, 100 Hz, 411 us pulse. */
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
    if (device == NULL || device->hal == NULL || device->hal->delay_ms == NULL) {
        return false;
    }
    uint8_t chip_id = 0u;
    if (!read_register(device, 0x00u, &chip_id, 1u) || chip_id != 0x24u) {
        return false;
    }
    if (!write_u8(device, 0x7eu, 0xb6u)) {
        return false;
    }
    device->hal->delay_ms(2u);
    /* BMI270 datasheet 4.4: leave advanced power save before loading the
     * mandatory 8 KiB configuration. The HAL timer has millisecond resolution. */
    if (!write_u8(device, 0x7cu, 0x00u)) {
        return false;
    }
    device->hal->delay_ms(1u); /* At least 450 us. */
    if (!write_u8(device, 0x59u, 0x00u)) {
        return false;
    }
    /* INIT_ADDR encodes a word address (low nibble, then upper eight bits).
     * Keep each burst within the register-write buffer's eight-byte limit. */
    for (size_t offset = 0u; offset < sizeof(whm_bmi270_config); offset += 8u) {
        const size_t word_address = offset / 2u;
        const uint8_t address_bytes[2] = {(uint8_t)(word_address & 0x0fu), (uint8_t)(word_address >> 4u)};
        if (!write_register(device, 0x5bu, address_bytes, sizeof(address_bytes)) ||
            !write_register(device, 0x5eu, &whm_bmi270_config[offset], 8u)) {
            return false;
        }
    }
    if (!write_u8(device, 0x59u, 0x01u)) {
        return false;
    }
    device->hal->delay_ms(20u);
    uint8_t internal_status = 0u;
    if (!read_register(device, 0x21u, &internal_status, 1u) || (internal_status & 0x0fu) != 0x01u) {
        return false;
    }
    if (!write_u8(device, 0x40u, 0xa8u) || /* 100 Hz, normal bandwidth. */
        !write_u8(device, 0x41u, 0x01u) || /* +/-4 g: 8192 LSB/g. */
        !write_u8(device, 0x7du, 0x04u)) { /* Accelerometer only. */
        return false;
    }
    device->hal->delay_ms(2u);
    return true;
}

static int16_t bmi270_raw_to_mg(uint8_t low, uint8_t high) {
    const uint16_t bits = (uint16_t)low | ((uint16_t)high << 8u);
    const int32_t raw = bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536;
    const int32_t scaled = raw * 1000;
    return (int16_t)((scaled + (scaled < 0 ? -4096 : 4096)) / 8192);
}

bool whm_bmi270_read_accel(const whm_i2c_device_t *device, int16_t *x_mg, int16_t *y_mg, int16_t *z_mg) {
    if (x_mg == NULL || y_mg == NULL || z_mg == NULL) {
        return false;
    }
    uint8_t bytes[6] = {0};
    if (!read_register(device, 0x0cu, bytes, sizeof(bytes))) {
        return false;
    }
    *x_mg = bmi270_raw_to_mg(bytes[0], bytes[1]);
    *y_mg = bmi270_raw_to_mg(bytes[2], bytes[3]);
    *z_mg = bmi270_raw_to_mg(bytes[4], bytes[5]);
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

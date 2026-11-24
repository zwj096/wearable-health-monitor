#include "whm_protocol.h"

#include <stddef.h>

static void put_u16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8u) & 0xffu);
    dst[2] = (uint8_t)((value >> 16u) & 0xffu);
    dst[3] = (uint8_t)(value >> 24u);
}

static uint16_t get_u16(const uint8_t *src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8u);
}

static uint32_t get_u32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8u) | ((uint32_t)src[2] << 16u) | ((uint32_t)src[3] << 24u);
}

uint16_t whm_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xffffu;
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8u;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ? (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

size_t whm_protocol_encode(const whm_result_t *result, uint8_t *out, size_t out_capacity) {
    if (result == NULL || out == NULL || out_capacity < WHM_PACKET_SIZE) {
        return 0u;
    }
    out[0] = WHM_PACKET_MAGIC;
    out[1] = WHM_PACKET_VERSION;
    out[2] = WHM_PACKET_SIZE - 2u;
    out[3] = result->flags;
    put_u16(&out[4], result->sequence);
    put_u32(&out[6], result->timestamp_ms);
    put_u16(&out[10], result->heart_rate_bpm_x10);
    put_u16(&out[12], (uint16_t)result->temperature_c_x100);
    put_u16(&out[14], result->motion_mg);
    out[16] = result->quality;
    out[17] = result->state;
    put_u16(&out[18], result->battery_mv);
    put_u16(&out[20], whm_crc16(out, WHM_PACKET_SIZE - 2u));
    return WHM_PACKET_SIZE;
}

bool whm_protocol_decode(const uint8_t *data, size_t length, whm_result_t *out) {
    if (data == NULL || out == NULL || length != WHM_PACKET_SIZE) {
        return false;
    }
    if (data[0] != WHM_PACKET_MAGIC || data[1] != WHM_PACKET_VERSION || data[2] != WHM_PACKET_SIZE - 2u) {
        return false;
    }
    if (get_u16(&data[20]) != whm_crc16(data, WHM_PACKET_SIZE - 2u)) {
        return false;
    }
    out->flags = data[3];
    out->sequence = get_u16(&data[4]);
    out->timestamp_ms = get_u32(&data[6]);
    out->heart_rate_bpm_x10 = get_u16(&data[10]);
    out->temperature_c_x100 = (int16_t)get_u16(&data[12]);
    out->motion_mg = get_u16(&data[14]);
    out->quality = data[16];
    out->state = data[17];
    out->battery_mv = get_u16(&data[18]);
    return true;
}

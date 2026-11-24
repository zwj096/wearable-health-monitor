#ifndef WHM_PROTOCOL_H
#define WHM_PROTOCOL_H

#include "whm_types.h"

#define WHM_PACKET_MAGIC 0xA5u
#define WHM_PACKET_VERSION 1u
#define WHM_PACKET_SIZE 22u

uint16_t whm_crc16(const uint8_t *data, size_t length);
size_t whm_protocol_encode(const whm_result_t *result, uint8_t *out, size_t out_capacity);
bool whm_protocol_decode(const uint8_t *data, size_t length, whm_result_t *out);

#endif

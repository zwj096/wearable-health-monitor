#ifndef WHM_HAL_H
#define WHM_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Hardware adapters implement these callbacks on nRF52840. */
typedef struct {
    bool (*i2c_write)(uint8_t address, const uint8_t *data, size_t length);
    bool (*i2c_read)(uint8_t address, uint8_t *data, size_t length);
    bool (*ble_send)(const uint8_t *data, size_t length);
    uint64_t (*monotonic_time_us)(void);
    void (*delay_ms)(uint32_t milliseconds);
} whm_hal_t;

#endif

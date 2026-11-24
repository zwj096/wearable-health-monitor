#include "whm_ring_buffer.h"

#include <string.h>

void whm_ring_init(whm_ring_buffer_t *ring, size_t capacity) {
    if (ring == NULL) {
        return;
    }
    memset(ring, 0, sizeof(*ring));
    ring->capacity = capacity > WHM_RING_MAX_CAPACITY ? WHM_RING_MAX_CAPACITY : capacity;
}

bool whm_ring_push(whm_ring_buffer_t *ring, const whm_sample_t *sample) {
    if (ring == NULL || sample == NULL || ring->capacity == 0) {
        return false;
    }
    const size_t write_index = (ring->head + ring->count) % ring->capacity;
    if (ring->count == ring->capacity) {
        ring->data[ring->head] = *sample;
        ring->head = (ring->head + 1u) % ring->capacity;
        ring->overflow_count++;
        return true;
    }
    ring->data[write_index] = *sample;
    ring->count++;
    return true;
}

bool whm_ring_pop(whm_ring_buffer_t *ring, whm_sample_t *sample) {
    if (ring == NULL || sample == NULL || ring->count == 0 || ring->capacity == 0) {
        return false;
    }
    *sample = ring->data[ring->head];
    ring->head = (ring->head + 1u) % ring->capacity;
    ring->count--;
    return true;
}

size_t whm_ring_count(const whm_ring_buffer_t *ring) {
    return ring == NULL ? 0u : ring->count;
}

size_t whm_ring_copy_ordered(const whm_ring_buffer_t *ring, whm_sample_t *out, size_t out_capacity) {
    if (ring == NULL || out == NULL || ring->capacity == 0 || out_capacity < ring->count) {
        return 0u;
    }
    for (size_t i = 0; i < ring->count; ++i) {
        out[i] = ring->data[(ring->head + i) % ring->capacity];
    }
    return ring->count;
}

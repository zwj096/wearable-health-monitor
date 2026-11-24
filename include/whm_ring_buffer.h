#ifndef WHM_RING_BUFFER_H
#define WHM_RING_BUFFER_H

#include "whm_types.h"

typedef struct {
    whm_sample_t data[WHM_RING_MAX_CAPACITY];
    size_t capacity;
    size_t head;
    size_t count;
    uint32_t overflow_count;
} whm_ring_buffer_t;

void whm_ring_init(whm_ring_buffer_t *ring, size_t capacity);
bool whm_ring_push(whm_ring_buffer_t *ring, const whm_sample_t *sample);
bool whm_ring_pop(whm_ring_buffer_t *ring, whm_sample_t *sample);
size_t whm_ring_count(const whm_ring_buffer_t *ring);
size_t whm_ring_copy_ordered(const whm_ring_buffer_t *ring, whm_sample_t *out, size_t out_capacity);

#endif

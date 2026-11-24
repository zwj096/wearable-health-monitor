#ifndef WHM_RUNTIME_H
#define WHM_RUNTIME_H

#include "whm_ring_buffer.h"
#include "whm_state.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    whm_ring_buffer_t window;
    whm_sample_t ordered[WHM_WINDOW_SAMPLES];
    whm_state_machine_t state;
    uint16_t sequence;
    uint64_t last_output_us;
    bool output_started;
} whm_runtime_t;

void whm_runtime_init(whm_runtime_t *runtime);
bool whm_runtime_push_sample(whm_runtime_t *runtime, const whm_sample_t *sample, uint8_t *packet, size_t packet_capacity, size_t *packet_length);

#endif

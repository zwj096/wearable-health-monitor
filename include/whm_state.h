#ifndef WHM_STATE_H
#define WHM_STATE_H

#include "whm_types.h"

typedef struct {
    whm_state_t state;
    uint8_t baseline_windows;
} whm_state_machine_t;

typedef struct {
    bool contact_ok;
    bool signal_ok;
    bool charging;
    bool sensor_fault;
} whm_state_input_t;

void whm_state_init(whm_state_machine_t *machine);
void whm_state_step(whm_state_machine_t *machine, const whm_state_input_t *input);

#endif

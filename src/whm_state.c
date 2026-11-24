#include "whm_state.h"

void whm_state_init(whm_state_machine_t *machine) {
    if (machine == NULL) {
        return;
    }
    machine->state = WHM_STATE_INIT;
    machine->baseline_windows = 0u;
}

void whm_state_step(whm_state_machine_t *machine, const whm_state_input_t *input) {
    if (machine == NULL || input == NULL) {
        return;
    }
    if (input->charging) {
        machine->state = WHM_STATE_CHARGING;
        machine->baseline_windows = 0u;
        return;
    }
    if (input->sensor_fault) {
        machine->state = WHM_STATE_FAULT;
        machine->baseline_windows = 0u;
        return;
    }
    if (machine->state == WHM_STATE_CHARGING || machine->state == WHM_STATE_FAULT) {
        machine->state = WHM_STATE_WAIT_CONTACT;
        machine->baseline_windows = 0u;
        return;
    }
    if (!input->contact_ok) {
        machine->state = WHM_STATE_WAIT_CONTACT;
        machine->baseline_windows = 0u;
        return;
    }
    if (!input->signal_ok) {
        if (machine->state == WHM_STATE_INIT || machine->state == WHM_STATE_WAIT_CONTACT) {
            machine->state = WHM_STATE_BASELINE;
        } else {
            machine->state = WHM_STATE_LOW_QUALITY;
        }
        if (machine->baseline_windows < 255u) {
            machine->baseline_windows++;
        }
        return;
    }
    machine->state = WHM_STATE_VALID;
    machine->baseline_windows = 0u;
}

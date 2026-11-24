#ifndef WHM_SIGNAL_H
#define WHM_SIGNAL_H

#include "whm_types.h"

bool whm_estimate_window(const whm_sample_t *samples, size_t count, whm_estimate_t *out);

#endif

#ifndef RUNTIME_H
#define RUNTIME_H

#include "scenario.h"

typedef struct {
    scenario input;
    uint32_t duration_ms;
    uint32_t processing_delay_ms;
} runtime_options;

int runtime_run(const runtime_options *options);

#endif

#ifndef SCENARIO_H
#define SCENARIO_H

#include "monitor.h"
#include <stdio.h>

#define SCENARIO_CAPACITY 128
#define MAX_DURATION_MS 60000

typedef struct {
    reading samples[SCENARIO_CAPACITY];
    size_t count;
} scenario;

bool scenario_load(const char *path, scenario *output);
bool scenario_read(FILE *file, scenario *output);
reading scenario_at(const scenario *input, uint32_t now_ms);
bool parse_unsigned(const char *text, uint32_t maximum, uint32_t *value);

#endif

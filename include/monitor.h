#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>
#include "tmp102.h"

enum {
    STATUS_VALID = 1,
    STATUS_OVERHEAT = 2,
    STATUS_STALE = 4,
    STATUS_SENSOR_ERROR = 8,
    STATUS_OVERFLOW = 16,
    STATUS_MASK = 31
};

typedef struct {
    int16_t trip_q4;
    int16_t clear_q4;
    uint32_t debounce_ms;
    uint32_t stale_ms;
} monitor_config;

typedef struct {
    uint32_t timestamp_ms;
    int16_t temperature_q4;
    i2c_result result;
} reading;

typedef struct {
    monitor_config config;
    uint32_t last_valid_ms;
    uint32_t last_sample_ms;
    uint32_t pending_since_ms;
    int16_t temperature_q4;
    uint8_t flags;
    bool received_sample;
    bool pending;
} monitor;

bool monitor_init(monitor *state, monitor_config config, uint32_t now_ms);
bool monitor_observe(monitor *state, reading sample, uint32_t now_ms);
void monitor_tick(monitor *state, uint32_t now_ms);

#endif

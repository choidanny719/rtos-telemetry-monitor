#include "monitor.h"
#include <limits.h>

bool monitor_init(monitor *state, monitor_config config, uint32_t now_ms)
{
    if (!state || config.clear_q4 >= config.trip_q4 || config.clear_q4 < -2048 ||
        config.trip_q4 > 2047 || config.debounce_ms == 0 ||
        config.debounce_ms > INT32_MAX || config.stale_ms == 0 || config.stale_ms > INT32_MAX) {
        return false;
    }
    *state = (monitor){.config = config, .last_valid_ms = now_ms};
    return true;
}

void monitor_tick(monitor *state, uint32_t now_ms)
{
    if ((uint32_t)(now_ms - state->last_valid_ms) >= state->config.stale_ms) {
        state->flags |= STATUS_STALE;
        state->pending = false;
    }
}

bool monitor_observe(monitor *state, reading sample, uint32_t now_ms)
{
    monitor_tick(state, now_ms);
    uint32_t age = now_ms - sample.timestamp_ms;
    uint32_t advance = sample.timestamp_ms - state->last_sample_ms;
    if (age >= state->config.stale_ms ||
        (state->received_sample && (advance == 0 || advance > INT32_MAX))) {
        return false;
    }
    state->received_sample = true;
    state->last_sample_ms = sample.timestamp_ms;
    if (sample.result != I2C_OK || sample.temperature_q4 < -2048 || sample.temperature_q4 > 2047) {
        state->flags |= STATUS_SENSOR_ERROR;
        state->pending = false;
        return true;
    }
    state->temperature_q4 = sample.temperature_q4;
    state->last_valid_ms = sample.timestamp_ms;
    state->flags = (uint8_t)((state->flags | STATUS_VALID) & ~(STATUS_STALE | STATUS_SENSOR_ERROR));
    bool overheated = (state->flags & STATUS_OVERHEAT) != 0;
    bool qualifies = overheated ? sample.temperature_q4 <= state->config.clear_q4
                               : sample.temperature_q4 >= state->config.trip_q4;
    if (!qualifies) {
        state->pending = false;
    } else if (!state->pending) {
        state->pending = true;
        state->pending_since_ms = sample.timestamp_ms;
    } else if ((uint32_t)(sample.timestamp_ms - state->pending_since_ms) >= state->config.debounce_ms) {
        state->flags ^= STATUS_OVERHEAT;
        state->pending = false;
    }
    return true;
}

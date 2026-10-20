#include "telemetry.h"

bool telemetry_encode(const telemetry *status, can_frame *frame)
{
    if (!status || !frame || (status->flags & ~STATUS_MASK) != 0 ||
        status->temperature_q4 < -2048 || status->temperature_q4 > 2047) {
        return false;
    }
    uint16_t temperature = (uint16_t)status->temperature_q4;
    *frame = (can_frame){.id = TELEMETRY_CAN_ID, .length = 8, .data = {
        (uint8_t)(temperature >> 8), (uint8_t)temperature,
        status->flags, status->sequence,
        (uint8_t)(status->timestamp_ms >> 24), (uint8_t)(status->timestamp_ms >> 16),
        (uint8_t)(status->timestamp_ms >> 8), (uint8_t)status->timestamp_ms
    }};
    return true;
}

bool telemetry_decode(const can_frame *frame, telemetry *status)
{
    if (!frame || !status || frame->id != TELEMETRY_CAN_ID || frame->length != 8 ||
        (frame->data[2] & ~STATUS_MASK) != 0) {
        return false;
    }
    int32_t value = (uint16_t)((uint16_t)frame->data[0] << 8 | frame->data[1]);
    if (value >= 32768) {
        value -= 65536;
    }
    if (value < -2048 || value > 2047) {
        return false;
    }
    *status = (telemetry){
        .temperature_q4 = (int16_t)value,
        .flags = frame->data[2],
        .sequence = frame->data[3],
        .timestamp_ms = (uint32_t)frame->data[4] << 24 | (uint32_t)frame->data[5] << 16 |
                        (uint32_t)frame->data[6] << 8 | frame->data[7]
    };
    return true;
}

void history_append(telemetry_history *history, telemetry status)
{
    size_t index = (history->start + history->count) % HISTORY_CAPACITY;
    history->records[index] = status;
    if (history->count == HISTORY_CAPACITY) {
        history->start = (history->start + 1) % HISTORY_CAPACITY;
    } else {
        ++history->count;
    }
}

bool history_get(const telemetry_history *history, size_t index, telemetry *status)
{
    if (!history || !status || index >= history->count) {
        return false;
    }
    *status = history->records[(history->start + index) % HISTORY_CAPACITY];
    return true;
}

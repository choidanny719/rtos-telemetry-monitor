#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "monitor.h"

#define TELEMETRY_CAN_ID 0x321
#define HISTORY_CAPACITY 16

typedef struct {
    uint16_t id;
    uint8_t length;
    uint8_t data[8];
} can_frame;

typedef struct {
    uint32_t timestamp_ms;
    int16_t temperature_q4;
    uint8_t flags;
    uint8_t sequence;
} telemetry;

typedef struct {
    telemetry records[HISTORY_CAPACITY];
    size_t start;
    size_t count;
} telemetry_history;

bool telemetry_encode(const telemetry *status, can_frame *frame);
bool telemetry_decode(const can_frame *frame, telemetry *status);
void history_append(telemetry_history *history, telemetry status);
bool history_get(const telemetry_history *history, size_t index, telemetry *status);

#endif

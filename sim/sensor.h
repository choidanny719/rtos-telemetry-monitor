#ifndef SENSOR_H
#define SENSOR_H

#include "tmp102.h"

typedef struct {
    uint8_t address;
    uint16_t configuration;
    int16_t temperature_q4;
    i2c_result failure;
} simulated_sensor;

void simulated_sensor_init(simulated_sensor *sensor);
i2c_result simulated_sensor_transfer(void *context, uint8_t address,
                                     const uint8_t *tx, size_t tx_size,
                                     uint8_t *rx, size_t rx_size);

#endif

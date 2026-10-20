#ifndef TMP102_H
#define TMP102_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    I2C_OK,
    I2C_NACK,
    I2C_TIMEOUT,
    I2C_INVALID
} i2c_result;

typedef i2c_result (*i2c_transfer)(void *context, uint8_t address,
                                   const uint8_t *tx, size_t tx_size,
                                   uint8_t *rx, size_t rx_size);

typedef struct {
    i2c_transfer transfer;
    void *context;
    uint8_t address;
} tmp102;

i2c_result tmp102_configure(const tmp102 *sensor);
i2c_result tmp102_read(const tmp102 *sensor, int16_t *temperature_q4);

#endif

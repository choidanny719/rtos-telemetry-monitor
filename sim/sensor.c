#include "sensor.h"

void simulated_sensor_init(simulated_sensor *sensor)
{
    *sensor = (simulated_sensor){.address = 0x48, .configuration = 0x60a0, .temperature_q4 = 400};
}

i2c_result simulated_sensor_transfer(void *context, uint8_t address,
                                     const uint8_t *tx, size_t tx_size,
                                     uint8_t *rx, size_t rx_size)
{
    simulated_sensor *sensor = context;
    if (!sensor || !tx || tx_size == 0 || (rx_size != 0 && !rx)) {
        return I2C_INVALID;
    }
    if (address != sensor->address) {
        return I2C_NACK;
    }
    if (sensor->failure != I2C_OK) {
        return sensor->failure;
    }
    if (tx_size == 1 && rx_size == 2) {
        uint16_t value;
        if (tx[0] == 0) {
            if ((sensor->configuration & 0x0110u) != 0) {
                return I2C_INVALID;
            }
            value = (uint16_t)((uint16_t)sensor->temperature_q4 << 4);
        } else if (tx[0] == 1) {
            value = sensor->configuration;
        } else {
            return I2C_INVALID;
        }
        rx[0] = (uint8_t)(value >> 8);
        rx[1] = (uint8_t)value;
        return I2C_OK;
    }
    if (tx_size == 3 && rx_size == 0 && tx[0] == 1) {
        uint16_t requested = (uint16_t)((uint16_t)tx[1] << 8 | tx[2]);
        sensor->configuration = (uint16_t)((requested & 0x9fd0u) | (sensor->configuration & 0x6020u));
        return I2C_OK;
    }
    return I2C_INVALID;
}

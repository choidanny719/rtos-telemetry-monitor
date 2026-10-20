#include "tmp102.h"

static int valid(const tmp102 *sensor)
{
    return sensor && sensor->transfer && sensor->address >= 0x48 && sensor->address <= 0x4b;
}

i2c_result tmp102_configure(const tmp102 *sensor)
{
    if (!valid(sensor)) {
        return I2C_INVALID;
    }
    uint8_t pointer = 1;
    uint8_t bytes[2];
    i2c_result result = sensor->transfer(sensor->context, sensor->address, &pointer, 1, bytes, 2);
    if (result != I2C_OK) {
        return result;
    }
    uint16_t configuration = (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
    configuration = (uint16_t)((configuration & ~0x01d0u) | 0x00c0u);
    uint8_t write[3] = {1, (uint8_t)(configuration >> 8), (uint8_t)configuration};
    return sensor->transfer(sensor->context, sensor->address, write, 3, NULL, 0);
}

i2c_result tmp102_read(const tmp102 *sensor, int16_t *temperature_q4)
{
    if (!valid(sensor) || !temperature_q4) {
        return I2C_INVALID;
    }
    uint8_t pointer = 0;
    uint8_t bytes[2];
    i2c_result result = sensor->transfer(sensor->context, sensor->address, &pointer, 1, bytes, 2);
    if (result != I2C_OK) {
        return result;
    }
    uint16_t raw = (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
    if ((raw & 0x000fu) != 0) {
        return I2C_INVALID;
    }
    int32_t value = raw >> 4;
    if (value >= 2048) {
        value -= 4096;
    }
    *temperature_q4 = (int16_t)value;
    return I2C_OK;
}

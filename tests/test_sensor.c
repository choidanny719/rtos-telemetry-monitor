#include "sensor.h"
#include "unity.h"

static simulated_sensor chip;
static tmp102 sensor;

void setUp(void)
{
    simulated_sensor_init(&chip);
    sensor = (tmp102){simulated_sensor_transfer, &chip, 0x48};
}

void tearDown(void) {}

static void configuration_sets_continuous_normal_mode_at_eight_hz(void)
{
    chip.configuration = 0x7f30;
    TEST_ASSERT_EQUAL(I2C_OK, tmp102_configure(&sensor));
    TEST_ASSERT_EQUAL_HEX16(0x7ee0, chip.configuration);
}

static void every_normal_mode_value_round_trips(void)
{
    for (int value = -2048; value <= 2047; ++value) {
        chip.temperature_q4 = (int16_t)value;
        int16_t actual = 0;
        TEST_ASSERT_EQUAL(I2C_OK, tmp102_read(&sensor, &actual));
        TEST_ASSERT_EQUAL_INT16(value, actual);
    }
}

static i2c_result fixture_transfer(void *context, uint8_t address, const uint8_t *tx,
                                 size_t tx_size, uint8_t *rx, size_t rx_size)
{
    TEST_ASSERT_EQUAL_HEX8(0x4b, address);
    TEST_ASSERT_EQUAL(1, tx_size);
    TEST_ASSERT_EQUAL_HEX8(0, tx[0]);
    TEST_ASSERT_EQUAL(2, rx_size);
    const uint8_t *bytes = context;
    rx[0] = bytes[0];
    rx[1] = bytes[1];
    return I2C_OK;
}

static void datasheet_vectors_decode_msb_first(void)
{
    uint8_t vectors[][2] = {{0x19, 0x10}, {0xff, 0xf0}, {0xe7, 0x00}, {0x7d, 0x00}};
    int16_t expected[] = {401, -1, -400, 2000};
    sensor.transfer = fixture_transfer;
    sensor.address = 0x4b;
    for (size_t i = 0; i < 4; ++i) {
        sensor.context = vectors[i];
        int16_t actual = 0;
        TEST_ASSERT_EQUAL(I2C_OK, tmp102_read(&sensor, &actual));
        TEST_ASSERT_EQUAL_INT16(expected[i], actual);
    }
}

static void invalid_mode_preserves_output(void)
{
    uint8_t bytes[] = {0x19, 0x11};
    sensor = (tmp102){fixture_transfer, bytes, 0x4b};
    int16_t temperature = 123;
    TEST_ASSERT_EQUAL(I2C_INVALID, tmp102_read(&sensor, &temperature));
    TEST_ASSERT_EQUAL_INT16(123, temperature);
}

static void bus_failures_propagate_without_changing_output(void)
{
    for (i2c_result failure = I2C_NACK; failure <= I2C_INVALID; ++failure) {
        chip.failure = failure;
        int16_t temperature = 123;
        TEST_ASSERT_EQUAL(failure, tmp102_configure(&sensor));
        TEST_ASSERT_EQUAL(failure, tmp102_read(&sensor, &temperature));
        TEST_ASSERT_EQUAL_INT16(123, temperature);
    }
}

static i2c_result write_failure(void *context, uint8_t address, const uint8_t *tx,
                              size_t tx_size, uint8_t *rx, size_t rx_size)
{
    if (tx_size == 3) {
        TEST_ASSERT_EQUAL_HEX8(1, tx[0]);
        TEST_ASSERT_EQUAL_HEX8(0x60, tx[1]);
        TEST_ASSERT_EQUAL_HEX8(0xe0, tx[2]);
        return I2C_TIMEOUT;
    }
    return simulated_sensor_transfer(context, address, tx, tx_size, rx, rx_size);
}

static void configuration_write_failure_propagates(void)
{
    sensor.transfer = write_failure;
    TEST_ASSERT_EQUAL(I2C_TIMEOUT, tmp102_configure(&sensor));
}

static void invalid_arguments_are_rejected(void)
{
    int16_t temperature;
    TEST_ASSERT_EQUAL(I2C_INVALID, tmp102_read(NULL, &temperature));
    TEST_ASSERT_EQUAL(I2C_INVALID, tmp102_configure(NULL));
    TEST_ASSERT_EQUAL(I2C_INVALID, tmp102_read(&sensor, NULL));
    sensor.address = 0x47;
    TEST_ASSERT_EQUAL(I2C_INVALID, tmp102_configure(&sensor));
    sensor.address = 0x4c;
    TEST_ASSERT_EQUAL(I2C_INVALID, tmp102_read(&sensor, &temperature));
    sensor.address = 0x48;
    sensor.transfer = NULL;
    TEST_ASSERT_EQUAL(I2C_INVALID, tmp102_configure(&sensor));
}

static void simulator_rejects_wrong_address_and_unsupported_transactions(void)
{
    uint8_t tx[] = {0, 0, 0};
    uint8_t rx[2];
    TEST_ASSERT_EQUAL(I2C_NACK, simulated_sensor_transfer(&chip, 0x49, tx, 1, rx, 2));
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(NULL, 0x48, tx, 1, rx, 2));
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(&chip, 0x48, NULL, 1, rx, 2));
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(&chip, 0x48, tx, 0, rx, 2));
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(&chip, 0x48, tx, 1, NULL, 2));
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(&chip, 0x48, tx, 2, rx, 2));
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(&chip, 0x48, tx, 3, NULL, 0));
    tx[0] = 2;
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(&chip, 0x48, tx, 1, rx, 2));
    tx[0] = 0;
    chip.configuration |= 0x0100;
    TEST_ASSERT_EQUAL(I2C_INVALID, simulated_sensor_transfer(&chip, 0x48, tx, 1, rx, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(configuration_sets_continuous_normal_mode_at_eight_hz);
    RUN_TEST(every_normal_mode_value_round_trips);
    RUN_TEST(datasheet_vectors_decode_msb_first);
    RUN_TEST(invalid_mode_preserves_output);
    RUN_TEST(bus_failures_propagate_without_changing_output);
    RUN_TEST(configuration_write_failure_propagates);
    RUN_TEST(invalid_arguments_are_rejected);
    RUN_TEST(simulator_rejects_wrong_address_and_unsupported_transactions);
    return UNITY_END();
}

#include "monitor.h"
#include "unity.h"

static monitor state;
static monitor_config config = {1280, 1200, 300, 400};

void setUp(void) { TEST_ASSERT_TRUE(monitor_init(&state, config, 0)); }
void tearDown(void) {}

static void observe(uint32_t at, int16_t temperature, i2c_result result)
{
    TEST_ASSERT_TRUE(monitor_observe(&state, (reading){at, temperature, result}, at));
}

static void normal_samples_and_negative_temperatures_stay_healthy(void)
{
    observe(0, -640, I2C_OK);
    observe(100, 1279, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
    TEST_ASSERT_EQUAL_INT16(1279, state.temperature_q4);
}

static void sustained_heat_trips_at_exact_debounce_boundary(void)
{
    observe(0, 1280, I2C_OK);
    observe(299, 1280, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
    observe(300, 1280, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID | STATUS_OVERHEAT, state.flags);
}

static void a_short_spike_resets_debounce(void)
{
    observe(0, 1400, I2C_OK);
    observe(200, 1279, I2C_OK);
    observe(300, 1400, I2C_OK);
    observe(500, 1400, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
    observe(600, 1400, I2C_OK);
    TEST_ASSERT_BITS_HIGH(STATUS_OVERHEAT, state.flags);
}

static void recovery_requires_cool_samples_and_hysteresis(void)
{
    observe(0, 1280, I2C_OK);
    observe(300, 1280, I2C_OK);
    observe(400, 1201, I2C_OK);
    observe(700, 1201, I2C_OK);
    TEST_ASSERT_BITS_HIGH(STATUS_OVERHEAT, state.flags);
    observe(800, 1200, I2C_OK);
    observe(1099, 1200, I2C_OK);
    TEST_ASSERT_BITS_HIGH(STATUS_OVERHEAT, state.flags);
    observe(1100, 1200, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
}

static void interrupted_recovery_keeps_alarm_latched(void)
{
    observe(0, 1280, I2C_OK);
    observe(300, 1280, I2C_OK);
    observe(400, 1100, I2C_OK);
    observe(600, 1201, I2C_OK);
    observe(700, 1100, I2C_OK);
    observe(900, 1100, I2C_OK);
    TEST_ASSERT_BITS_HIGH(STATUS_OVERHEAT, state.flags);
}

static void failed_reads_preserve_temperature_and_alarm(void)
{
    observe(0, 1400, I2C_OK);
    observe(300, 1400, I2C_OK);
    observe(400, 0, I2C_NACK);
    observe(500, 0, I2C_TIMEOUT);
    TEST_ASSERT_EQUAL_INT16(1400, state.temperature_q4);
    TEST_ASSERT_BITS_HIGH(STATUS_OVERHEAT | STATUS_SENSOR_ERROR, state.flags);
    monitor_tick(&state, 700);
    TEST_ASSERT_BITS_HIGH(STATUS_STALE | STATUS_OVERHEAT, state.flags);
    observe(800, 400, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID | STATUS_OVERHEAT, state.flags);
    observe(1100, 400, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
}

static void stale_starts_at_boundary_even_without_any_sample(void)
{
    monitor_tick(&state, 399);
    TEST_ASSERT_EQUAL(0, state.flags);
    monitor_tick(&state, 400);
    TEST_ASSERT_EQUAL(STATUS_STALE, state.flags);
    observe(500, 400, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
    monitor_tick(&state, 899);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
    monitor_tick(&state, 900);
    TEST_ASSERT_EQUAL(STATUS_VALID | STATUS_STALE, state.flags);
}

static void missing_or_failed_samples_cannot_complete_debounce(void)
{
    observe(0, 1400, I2C_OK);
    observe(200, 0, I2C_NACK);
    observe(300, 1400, I2C_OK);
    observe(500, 1400, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
    observe(900, 1400, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_VALID, state.flags);
}

static void old_duplicate_and_future_samples_are_rejected(void)
{
    observe(100, 400, I2C_OK);
    TEST_ASSERT_FALSE(monitor_observe(&state, (reading){100, 900, I2C_OK}, 200));
    TEST_ASSERT_FALSE(monitor_observe(&state, (reading){99, 900, I2C_OK}, 200));
    TEST_ASSERT_FALSE(monitor_observe(&state, (reading){201, 900, I2C_OK}, 200));
    TEST_ASSERT_FALSE(monitor_observe(&state, (reading){200, 900, I2C_OK}, 600));
    TEST_ASSERT_EQUAL_INT16(400, state.temperature_q4);
    TEST_ASSERT_BITS_HIGH(STATUS_STALE, state.flags);
}

static void elapsed_time_survives_uint32_wrap(void)
{
    TEST_ASSERT_TRUE(monitor_init(&state, config, UINT32_MAX - 200));
    observe(UINT32_MAX - 200, 1400, I2C_OK);
    observe(UINT32_MAX - 1, 1400, I2C_OK);
    observe(99, 1400, I2C_OK);
    TEST_ASSERT_BITS_HIGH(STATUS_OVERHEAT, state.flags);
    monitor_tick(&state, 499);
    TEST_ASSERT_BITS_HIGH(STATUS_STALE, state.flags);
}

static void invalid_configuration_and_temperatures_are_rejected(void)
{
    TEST_ASSERT_FALSE(monitor_init(NULL, config, 0));
    TEST_ASSERT_FALSE(monitor_init(&state, (monitor_config){1200, 1200, 300, 400}, 0));
    TEST_ASSERT_FALSE(monitor_init(&state, (monitor_config){2048, 1200, 300, 400}, 0));
    TEST_ASSERT_FALSE(monitor_init(&state, (monitor_config){1280, -2049, 300, 400}, 0));
    TEST_ASSERT_FALSE(monitor_init(&state, (monitor_config){1280, 1200, 0, 400}, 0));
    TEST_ASSERT_FALSE(monitor_init(&state, (monitor_config){1280, 1200, 300, 0}, 0));
    TEST_ASSERT_FALSE(monitor_init(&state, (monitor_config){1280, 1200, UINT32_MAX, 400}, 0));
    TEST_ASSERT_FALSE(monitor_init(&state, (monitor_config){1280, 1200, 300, UINT32_MAX}, 0));
    observe(0, 2048, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_SENSOR_ERROR, state.flags);
    observe(100, -2049, I2C_OK);
    TEST_ASSERT_EQUAL(STATUS_SENSOR_ERROR, state.flags);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(normal_samples_and_negative_temperatures_stay_healthy);
    RUN_TEST(sustained_heat_trips_at_exact_debounce_boundary);
    RUN_TEST(a_short_spike_resets_debounce);
    RUN_TEST(recovery_requires_cool_samples_and_hysteresis);
    RUN_TEST(interrupted_recovery_keeps_alarm_latched);
    RUN_TEST(failed_reads_preserve_temperature_and_alarm);
    RUN_TEST(stale_starts_at_boundary_even_without_any_sample);
    RUN_TEST(missing_or_failed_samples_cannot_complete_debounce);
    RUN_TEST(old_duplicate_and_future_samples_are_rejected);
    RUN_TEST(elapsed_time_survives_uint32_wrap);
    RUN_TEST(invalid_configuration_and_temperatures_are_rejected);
    return UNITY_END();
}

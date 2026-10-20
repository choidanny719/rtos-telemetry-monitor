#include "telemetry.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void frame_matches_independent_golden_vector(void)
{
    telemetry status = {0x01020304, -400, STATUS_VALID | STATUS_OVERHEAT, 255};
    can_frame frame;
    uint8_t expected[] = {0xfe, 0x70, 3, 0xff, 1, 2, 3, 4};
    TEST_ASSERT_TRUE(telemetry_encode(&status, &frame));
    TEST_ASSERT_EQUAL_HEX16(0x321, frame.id);
    TEST_ASSERT_EQUAL(8, frame.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame.data, 8);
}

static void all_temperatures_and_flags_round_trip(void)
{
    for (int value = -2048; value <= 2047; ++value) {
        telemetry original = {UINT32_MAX, (int16_t)value, (uint8_t)((value + 2048) % 32), (uint8_t)value};
        telemetry decoded;
        can_frame frame;
        TEST_ASSERT_TRUE(telemetry_encode(&original, &frame));
        TEST_ASSERT_TRUE(telemetry_decode(&frame, &decoded));
        TEST_ASSERT_EQUAL_INT16(value, decoded.temperature_q4);
        TEST_ASSERT_EQUAL(original.flags, decoded.flags);
        TEST_ASSERT_EQUAL(original.sequence, decoded.sequence);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, decoded.timestamp_ms);
    }
}

static void malformed_frames_and_invalid_input_preserve_output(void)
{
    telemetry status = {0, 0, 0, 0};
    can_frame frame;
    TEST_ASSERT_TRUE(telemetry_encode(&status, &frame));
    TEST_ASSERT_FALSE(telemetry_encode(NULL, &frame));
    TEST_ASSERT_FALSE(telemetry_encode(&status, NULL));
    TEST_ASSERT_FALSE(telemetry_decode(NULL, &status));
    TEST_ASSERT_FALSE(telemetry_decode(&frame, NULL));
    status.flags = 32;
    TEST_ASSERT_FALSE(telemetry_encode(&status, &frame));
    status.flags = 0;
    status.temperature_q4 = 2048;
    TEST_ASSERT_FALSE(telemetry_encode(&status, &frame));
    status.temperature_q4 = -2049;
    TEST_ASSERT_FALSE(telemetry_encode(&status, &frame));
    status.temperature_q4 = 123;
    frame.id = 0x322;
    TEST_ASSERT_FALSE(telemetry_decode(&frame, &status));
    frame.id = 0x321;
    for (int length = 0; length <= 9; ++length) {
        if (length == 8) continue;
        frame.length = (uint8_t)length;
        TEST_ASSERT_FALSE(telemetry_decode(&frame, &status));
    }
    frame.length = 8;
    frame.data[2] = 0x80;
    TEST_ASSERT_FALSE(telemetry_decode(&frame, &status));
    frame.data[2] = 0;
    frame.data[0] = 0x08;
    TEST_ASSERT_FALSE(telemetry_decode(&frame, &status));
    frame.data[0] = 0xf7;
    TEST_ASSERT_FALSE(telemetry_decode(&frame, &status));
    TEST_ASSERT_EQUAL_INT16(123, status.temperature_q4);
}

static void history_keeps_latest_sixteen_in_order_after_multiple_wraps(void)
{
    telemetry_history history = {0};
    telemetry status;
    TEST_ASSERT_FALSE(history_get(&history, 0, &status));
    TEST_ASSERT_FALSE(history_get(NULL, 0, &status));
    for (uint32_t i = 0; i < 100; ++i) {
        history_append(&history, (telemetry){.timestamp_ms = i});
        TEST_ASSERT_EQUAL(i < 16 ? i + 1 : 16, history.count);
        TEST_ASSERT_TRUE(history_get(&history, history.count - 1, &status));
        TEST_ASSERT_EQUAL_UINT32(i, status.timestamp_ms);
    }
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_TRUE(history_get(&history, i, &status));
        TEST_ASSERT_EQUAL_UINT32(84 + i, status.timestamp_ms);
    }
    TEST_ASSERT_FALSE(history_get(&history, 16, &status));
    TEST_ASSERT_FALSE(history_get(&history, SIZE_MAX, &status));
    TEST_ASSERT_FALSE(history_get(&history, 0, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(frame_matches_independent_golden_vector);
    RUN_TEST(all_temperatures_and_flags_round_trip);
    RUN_TEST(malformed_frames_and_invalid_input_preserve_output);
    RUN_TEST(history_keeps_latest_sixteen_in_order_after_multiple_wraps);
    return UNITY_END();
}

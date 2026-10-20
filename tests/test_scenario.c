#include "scenario.h"
#include "unity.h"
#include <string.h>

static FILE *file;
static scenario input;

void setUp(void)
{
    file = tmpfile();
    TEST_ASSERT_NOT_NULL(file);
    input = (scenario){0};
}

void tearDown(void) { fclose(file); }

static bool read_text(const char *text)
{
    fputs(text, file);
    rewind(file);
    return scenario_read(file, &input);
}

static void records_hold_until_the_next_timestamp(void)
{
    TEST_ASSERT_TRUE(read_text("at_ms,temperature_q4,status\n0,400,ok\n100,-1,nack\n200,1280,timeout\n"));
    TEST_ASSERT_EQUAL(3, input.count);
    TEST_ASSERT_EQUAL_INT16(400, scenario_at(&input, 0).temperature_q4);
    TEST_ASSERT_EQUAL_INT16(400, scenario_at(&input, 99).temperature_q4);
    TEST_ASSERT_EQUAL(I2C_NACK, scenario_at(&input, 100).result);
    TEST_ASSERT_EQUAL(I2C_NACK, scenario_at(&input, 199).result);
    TEST_ASSERT_EQUAL(I2C_TIMEOUT, scenario_at(&input, 200).result);
    TEST_ASSERT_EQUAL(I2C_TIMEOUT, scenario_at(&input, 60000).result);
    TEST_ASSERT_EQUAL_UINT32(60000, scenario_at(&input, 60000).timestamp_ms);
}

static void crlf_and_a_missing_final_newline_are_accepted(void)
{
    TEST_ASSERT_TRUE(read_text("at_ms,temperature_q4,status\r\n0,-2048,ok\r\n60000,2047,ok"));
    TEST_ASSERT_EQUAL_INT16(-2048, scenario_at(&input, 0).temperature_q4);
    TEST_ASSERT_EQUAL_INT16(2047, scenario_at(&input, 60000).temperature_q4);
}

static void maximum_record_count_is_accepted(void)
{
    fputs("at_ms,temperature_q4,status\n", file);
    for (unsigned i = 0; i < SCENARIO_CAPACITY; ++i) fprintf(file, "%u,400,ok\n", i);
    rewind(file);
    TEST_ASSERT_TRUE(scenario_read(file, &input));
    TEST_ASSERT_EQUAL(SCENARIO_CAPACITY, input.count);
}

static void excess_records_are_rejected_without_overwriting_output(void)
{
    input.count = 12;
    fputs("at_ms,temperature_q4,status\n", file);
    for (unsigned i = 0; i <= SCENARIO_CAPACITY; ++i) fprintf(file, "%u,400,ok\n", i);
    rewind(file);
    TEST_ASSERT_FALSE(scenario_read(file, &input));
    TEST_ASSERT_EQUAL(12, input.count);
}

static void malformed_files_are_rejected(void)
{
    const char *cases[] = {
        "", "bad header\n0,400,ok\n", "at_ms,temperature_q4,status\n",
        "at_ms,temperature_q4,status\n1,400,ok\n",
        "at_ms,temperature_q4,status\n0,400,ok\n0,400,ok\n",
        "at_ms,temperature_q4,status\n0,400,ok\n100,400,ok\n99,400,ok\n",
        "at_ms,temperature_q4,status\n0,400,ok\n60001,400,ok\n",
        "at_ms,temperature_q4,status\n-1,400,ok\n",
        "at_ms,temperature_q4,status\n0,2048,ok\n",
        "at_ms,temperature_q4,status\n0,-2049,ok\n",
        "at_ms,temperature_q4,status\n0,99999999999999999999999999,ok\n",
        "at_ms,temperature_q4,status\n0,1.5,ok\n",
        "at_ms,temperature_q4,status\n0,,ok\n",
        "at_ms,temperature_q4,status\n0,400,error\n",
        "at_ms,temperature_q4,status\n0,400,ok,extra\n",
        "at_ms,temperature_q4,status\n0,400\n",
        "at_ms,temperature_q4,status\n0\n",
        "at_ms,temperature_q4,status\n\n",
        "at_ms,temperature_q4,status\n0,400,ok\rjunk\n"
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        FILE *source = tmpfile();
        TEST_ASSERT_NOT_NULL(source);
        fputs(cases[i], source);
        rewind(source);
        bool valid = scenario_read(source, &input);
        fclose(source);
        TEST_ASSERT_FALSE_MESSAGE(valid, cases[i]);
    }
}

static void oversized_lines_and_embedded_nuls_are_rejected(void)
{
    fputs("at_ms,temperature_q4,status\n", file);
    for (size_t i = 0; i < 128; ++i) fputc('0', file);
    rewind(file);
    TEST_ASSERT_FALSE(scenario_read(file, &input));
    FILE *binary = tmpfile();
    TEST_ASSERT_NOT_NULL(binary);
    const char payload[] = "at_ms,temperature_q4,status\n0,400,ok\0junk\n";
    fwrite(payload, 1, sizeof(payload) - 1, binary);
    rewind(binary);
    bool valid = scenario_read(binary, &input);
    fclose(binary);
    TEST_ASSERT_FALSE(valid);
}

static void numeric_options_reject_junk_signs_and_overflow(void)
{
    uint32_t value = 123;
    const char *invalid[] = {"", "-1", "+1", " 1", "1 ", "1ms", "1.5", "1001", "99999999999999999999999999"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        TEST_ASSERT_FALSE(parse_unsigned(invalid[i], 1000, &value));
        TEST_ASSERT_EQUAL_UINT32(123, value);
    }
    TEST_ASSERT_TRUE(parse_unsigned("0", 1000, &value));
    TEST_ASSERT_EQUAL_UINT32(0, value);
    TEST_ASSERT_TRUE(parse_unsigned("1000", 1000, &value));
    TEST_ASSERT_EQUAL_UINT32(1000, value);
    TEST_ASSERT_FALSE(parse_unsigned(NULL, 1000, &value));
    TEST_ASSERT_FALSE(parse_unsigned("1", 1000, NULL));
}

static void invalid_handles_and_missing_files_are_rejected(void)
{
    TEST_ASSERT_FALSE(scenario_read(NULL, &input));
    TEST_ASSERT_FALSE(scenario_read(file, NULL));
    TEST_ASSERT_FALSE(scenario_load(NULL, &input));
    TEST_ASSERT_FALSE(scenario_load("missing", NULL));
    TEST_ASSERT_FALSE(scenario_load("/missing-directory-for-telemetry-test/input.csv", &input));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(records_hold_until_the_next_timestamp);
    RUN_TEST(crlf_and_a_missing_final_newline_are_accepted);
    RUN_TEST(maximum_record_count_is_accepted);
    RUN_TEST(excess_records_are_rejected_without_overwriting_output);
    RUN_TEST(malformed_files_are_rejected);
    RUN_TEST(oversized_lines_and_embedded_nuls_are_rejected);
    RUN_TEST(numeric_options_reject_junk_signs_and_overflow);
    RUN_TEST(invalid_handles_and_missing_files_are_rejected);
    return UNITY_END();
}

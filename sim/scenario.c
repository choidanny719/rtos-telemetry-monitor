#include "scenario.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool parse_unsigned(const char *text, uint32_t maximum, uint32_t *value)
{
    if (!text || !value || !isdigit((unsigned char)*text)) {
        return false;
    }
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || *end || parsed > maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_row(char *line, reading *sample)
{
    char *temperature = strchr(line, ',');
    if (!temperature) return false;
    *temperature++ = '\0';
    char *status = strchr(temperature, ',');
    if (!status) return false;
    *status++ = '\0';
    if (!parse_unsigned(line, MAX_DURATION_MS, &sample->timestamp_ms)) return false;
    const char *digits = *temperature == '-' ? temperature + 1 : temperature;
    if (!isdigit((unsigned char)*digits)) return false;
    errno = 0;
    char *end;
    long value = strtol(temperature, &end, 10);
    if (errno || *end || value < -2048 || value > 2047) return false;
    sample->temperature_q4 = (int16_t)value;
    if (strcmp(status, "ok") == 0) sample->result = I2C_OK;
    else if (strcmp(status, "nack") == 0) sample->result = I2C_NACK;
    else if (strcmp(status, "timeout") == 0) sample->result = I2C_TIMEOUT;
    else return false;
    return true;
}

static int read_line(FILE *file, char *line, size_t capacity)
{
    size_t length = 0;
    int next;
    while ((next = fgetc(file)) != EOF && next != '\n') {
        if (next == 0 || length + 1 == capacity) return -1;
        line[length++] = (char)next;
    }
    if (ferror(file)) return -1;
    if (next == EOF && length == 0) return 0;
    if (length && line[length - 1] == '\r') --length;
    line[length] = '\0';
    return 1;
}

bool scenario_read(FILE *file, scenario *output)
{
    if (!file || !output) return false;
    scenario parsed = {0};
    char line[128];
    bool valid = read_line(file, line, sizeof(line)) == 1 &&
                 strcmp(line, "at_ms,temperature_q4,status") == 0;
    int result = 0;
    while (valid && (result = read_line(file, line, sizeof(line))) == 1) {
        if (parsed.count == SCENARIO_CAPACITY || !parse_row(line, &parsed.samples[parsed.count])) {
            valid = false;
            break;
        }
        uint32_t at = parsed.samples[parsed.count].timestamp_ms;
        if ((parsed.count == 0 && at != 0) ||
            (parsed.count != 0 && at <= parsed.samples[parsed.count - 1].timestamp_ms)) {
            valid = false;
            break;
        }
        ++parsed.count;
    }
    valid = valid && result == 0 && parsed.count != 0;
    if (valid) *output = parsed;
    return valid;
}

bool scenario_load(const char *path, scenario *output)
{
    if (!path || !output) return false;
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool valid = scenario_read(file, output);
    fclose(file);
    return valid;
}

reading scenario_at(const scenario *input, uint32_t now_ms)
{
    size_t low = 0;
    size_t high = input->count;
    while (low + 1 < high) {
        size_t middle = low + (high - low) / 2;
        if (input->samples[middle].timestamp_ms <= now_ms) low = middle;
        else high = middle;
    }
    reading sample = input->samples[low];
    sample.timestamp_ms = now_ms;
    return sample;
}

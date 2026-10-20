#include "runtime.h"
#include <stdio.h>
#include <string.h>

static int usage(FILE *stream, int code)
{
    fputs("Usage: telemetry_monitor [--scenario FILE] [--duration-ms 500..60000] "
          "[--processing-delay-ms 0..1000]\n", stream);
    return code;
}

int main(int argc, char **argv)
{
    runtime_options options = {
        .input = {.samples = {{0, 400, I2C_OK}}, .count = 1},
        .duration_ms = 2500
    };
    unsigned seen = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 && argc == 2) return usage(stdout, 0);
        if (i + 1 == argc) return usage(stderr, 2);
        if (strcmp(argv[i], "--scenario") == 0 && !(seen & 1)) {
            seen |= 1;
            if (!scenario_load(argv[++i], &options.input)) {
                fprintf(stderr, "Invalid scenario: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--duration-ms") == 0 && !(seen & 2)) {
            seen |= 2;
            if (!parse_unsigned(argv[++i], MAX_DURATION_MS, &options.duration_ms) ||
                options.duration_ms < 500) return usage(stderr, 2);
        } else if (strcmp(argv[i], "--processing-delay-ms") == 0 && !(seen & 4)) {
            seen |= 4;
            if (!parse_unsigned(argv[++i], 1000, &options.processing_delay_ms)) return usage(stderr, 2);
        } else {
            return usage(stderr, 2);
        }
    }
    return runtime_run(&options);
}

# RTOS Telemetry Monitor

A C temperature monitor running on the FreeRTOS POSIX port. Three tasks read a simulated TMP102 over an I²C interface, detect faults, and publish eight-byte CAN payloads. Runs on Linux or WSL without hardware.

## Build and run

Requires GCC, Git, CMake 3.24+, Make, and Python 3.10+. CMake downloads pinned versions of FreeRTOS 11.2.0 and Unity 2.6.1 on the first build.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/telemetry_monitor --scenario scenarios/overheat.csv | python3 tools/decode.py
```

The default run lasts 2.5 seconds at 25°C. Scenarios include normal readings, a brief temperature spike, sustained overheating, and a disconnected sensor that recovers.

```sh
./build/telemetry_monitor --scenario scenarios/disconnected.csv
./build/telemetry_monitor --duration-ms 1500 --processing-delay-ms 500
```

The second command deliberately slows processing to demonstrate queue overflow. Duration accepts 500–60,000 ms; processing delay accepts 0–1,000 ms. Shutdown drains the queue, so overload runs can take longer than the acquisition duration.

## Task design

| Task | Priority | Scheduling | Responsibility |
| --- | --- | --- | --- |
| Acquisition | 3 | Every 125 ms with `xTaskDelayUntil` | Read the sensor and send timestamped results to a four-entry queue |
| Processing | 2 | Queue receive with a 50 ms timeout | Update fault state and reject stale or reordered samples |
| Telemetry | 1 | Every 100 ms | Check freshness, snapshot state, retain 16 frames, and write JSON lines |

A priority-inheriting mutex protects state and counters. Event-group bits coordinate completion; tasks block between work. FreeRTOS also creates its idle task. Only telemetry writes to stdout while the scheduler runs. The final summary reports task activity, queue drops, rejected samples, and retained history.

At ≥80°C, consecutive valid readings must span 300 ms to latch an overheat alarm. Clearing it requires readings at ≤75°C for another 300 ms. Read failures cancel pending transitions and preserve the last temperature and latched alarm. After 400 ms without a valid reading, data becomes stale. Startup data remains invalid until the first successful read. Queue overflow drops the newest sample and latches an overflow flag for the rest of the run.

## Sensor and wire format

The driver follows the [TI TMP102 datasheet](https://www.ti.com/lit/ds/symlink/tmp102.pdf): register-pointer reads, MSB-first transfers, signed 12-bit temperatures, and configuration for continuous conversion at 8 Hz. It accepts addresses 0x48–0x4B and propagates NACKs and timeouts. The simulator models temperature/configuration registers; the bus callback can be replaced by a hardware transport.

CAN ID `0x321`, eight data bytes, multi-byte fields in big-endian order:

| Bytes | Meaning |
| --- | --- |
| 0–1 | Signed temperature in units of 1/16°C |
| 2 | Flags: bit 0 valid, 1 overheat, 2 stale, 3 sensor error, 4 overflow |
| 3 | Frame sequence, wrapping after 255 |
| 4–7 | Milliseconds since scheduler start |

`valid` means a temperature has been received; check `stale` and `sensor_error` before using it. The timestamp is the frame publication time. Reserved flag bits are rejected by both decoders. Raw output contains JSON records such as `{"type":"frame","id":801,"data":"0190010000000000"}`.

Scenario CSV files have the header `at_ms,temperature_q4,status`. Start at time zero and use strictly increasing timestamps, at most 128 rows, and status `ok`, `nack`, or `timeout`. Each row remains active until the next one; the final row persists. Temperatures use signed 12-bit raw units: `400` is 25°C and `-160` is −10°C.

## Tests

CTest runs 31 C tests, a scheduler smoke test, and 13 Python tests. Coverage includes every normal-mode sensor value, bus failures, debounce boundaries, hysteresis, clock wraparound, malformed frames and CSV input, history wraparound, fault recovery, and queue saturation. End-to-end tests run the actual FreeRTOS scheduler and independently unpack its CAN payloads.

```sh
cmake -S . -B build-sanitize -DBUILD_RUNTIME=OFF -DENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

CI runs the full release suite and separate AddressSanitizer/UndefinedBehaviorSanitizer tests. Sanitizers cover the C modules without the POSIX scheduler's signal/thread machinery.

This is software-in-the-loop testing. CAN is serialized to stdout, and I²C transactions use an in-memory device. Electrical behavior, conversion latency, bus arbitration, interrupt timing, and STM32 deployment are not simulated or validated. The host scheduler is useful for exercising RTOS coordination, not for measuring hardware deadlines.

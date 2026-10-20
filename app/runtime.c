#include "runtime.h"
#include "sensor.h"
#include "telemetry.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <inttypes.h>
#include <stdio.h>

enum { ACQUISITION_DONE = 1, PROCESSING_DONE = 2, QUEUE_CAPACITY = 4 };

typedef struct {
    uint32_t acquired;
    uint32_t processed;
    uint32_t rejected;
    uint32_t dropped;
    uint32_t read_errors;
    uint32_t processing_cycles;
    uint32_t frames;
    uint32_t max_queue_depth;
} counters;

typedef struct {
    const runtime_options *options;
    QueueHandle_t readings;
    SemaphoreHandle_t mutex;
    EventGroupHandle_t events;
    TaskHandle_t telemetry_task;
    simulated_sensor chip;
    tmp102 sensor;
    monitor state;
    counters counts;
    telemetry_history history;
    TickType_t start;
    bool finished;
} application;

static application app;

static uint32_t elapsed(void)
{
    return (uint32_t)((xTaskGetTickCount() - app.start) * portTICK_PERIOD_MS);
}

static void lock(void)
{
    configASSERT(xSemaphoreTake(app.mutex, portMAX_DELAY) == pdTRUE);
}

static void unlock(void)
{
    configASSERT(xSemaphoreGive(app.mutex) == pdTRUE);
}

static void acquisition_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    while (elapsed() < app.options->duration_ms) {
        uint32_t now = elapsed();
        reading sample = scenario_at(&app.options->input, now);
        app.chip.temperature_q4 = sample.temperature_q4;
        app.chip.failure = sample.result;
        sample.temperature_q4 = 0;
        sample.result = tmp102_read(&app.sensor, &sample.temperature_q4);
        BaseType_t queued = xQueueSend(app.readings, &sample, 0);
        uint32_t depth = (uint32_t)uxQueueMessagesWaiting(app.readings);
        lock();
        ++app.counts.acquired;
        if (sample.result != I2C_OK) ++app.counts.read_errors;
        if (queued != pdTRUE) {
            ++app.counts.dropped;
            app.state.flags |= STATUS_OVERFLOW;
        }
        if (depth > app.counts.max_queue_depth) app.counts.max_queue_depth = depth;
        unlock();
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(125));
    }
    xEventGroupSetBits(app.events, ACQUISITION_DONE);
    vTaskDelete(NULL);
}

static void processing_task(void *argument)
{
    (void)argument;
    for (;;) {
        reading sample;
        BaseType_t received = xQueueReceive(app.readings, &sample, pdMS_TO_TICKS(50));
        lock();
        ++app.counts.processing_cycles;
        if (received == pdTRUE) {
            ++app.counts.processed;
            if (!monitor_observe(&app.state, sample, elapsed())) ++app.counts.rejected;
        } else {
            monitor_tick(&app.state, elapsed());
        }
        unlock();
        if ((xEventGroupGetBits(app.events) & ACQUISITION_DONE) && uxQueueMessagesWaiting(app.readings) == 0) {
            break;
        }
        if (received == pdTRUE && app.options->processing_delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(app.options->processing_delay_ms));
        }
    }
    xEventGroupSetBits(app.events, PROCESSING_DONE);
    vTaskDelete(NULL);
}

static void emit_frame(void)
{
    lock();
    monitor_tick(&app.state, elapsed());
    telemetry status = {
        .timestamp_ms = elapsed(), .temperature_q4 = app.state.temperature_q4,
        .flags = app.state.flags, .sequence = (uint8_t)app.counts.frames
    };
    ++app.counts.frames;
    unlock();
    can_frame frame;
    configASSERT(telemetry_encode(&status, &frame));
    history_append(&app.history, status);
    printf("{\"type\":\"frame\",\"id\":%u,\"data\":\"", (unsigned)frame.id);
    for (size_t i = 0; i < frame.length; ++i) printf("%02x", frame.data[i]);
    puts("\"}");
    fflush(stdout);
}

static void telemetry_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        emit_frame();
        EventBits_t done = xEventGroupGetBits(app.events);
        if ((done & (ACQUISITION_DONE | PROCESSING_DONE)) == (ACQUISITION_DONE | PROCESSING_DONE)) {
            app.finished = true;
            vTaskEndScheduler();
        }
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
    }
}

void vApplicationMallocFailedHook(void)
{
    abort();
}

int runtime_run(const runtime_options *options)
{
    app = (application){.options = options};
    simulated_sensor_init(&app.chip);
    app.sensor = (tmp102){simulated_sensor_transfer, &app.chip, 0x48};
    if (tmp102_configure(&app.sensor) != I2C_OK ||
        !monitor_init(&app.state, (monitor_config){1280, 1200, 300, 400}, 0)) return 1;
    app.readings = xQueueCreate(QUEUE_CAPACITY, sizeof(reading));
    app.mutex = xSemaphoreCreateMutex();
    app.events = xEventGroupCreate();
    configASSERT(app.readings && app.mutex && app.events);
    configASSERT(xTaskCreate(acquisition_task, "acquisition", 2048, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(processing_task, "processing", 2048, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(telemetry_task, "telemetry", 2048, NULL, 1, &app.telemetry_task) == pdPASS);
    app.start = xTaskGetTickCount();
    vTaskStartScheduler();
    if (!app.finished) return 1;
    vTaskDelete(app.telemetry_task);
    telemetry oldest;
    configASSERT(history_get(&app.history, 0, &oldest));
    printf("{\"type\":\"summary\",\"acquired\":%" PRIu32 ",\"processed\":%" PRIu32
           ",\"rejected\":%" PRIu32 ",\"dropped\":%" PRIu32 ",\"read_errors\":%" PRIu32
           ",\"processing_cycles\":%" PRIu32 ",\"frames\":%" PRIu32
           ",\"max_queue_depth\":%" PRIu32 ",\"history_count\":%zu,\"history_oldest_ms\":%" PRIu32 "}\n",
           app.counts.acquired, app.counts.processed, app.counts.rejected, app.counts.dropped,
           app.counts.read_errors, app.counts.processing_cycles, app.counts.frames,
           app.counts.max_queue_depth, app.history.count, oldest.timestamp_ms);
    vQueueDelete(app.readings);
    vSemaphoreDelete(app.mutex);
    vEventGroupDelete(app.events);
    return 0;
}

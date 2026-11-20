#include "board.h"
#include "queue.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef ANALYSIS_AXIS
#define ANALYSIS_AXIS 2
#endif
_Static_assert(ANALYSIS_AXIS >= 0 && ANALYSIS_AXIS < 3, "Select axis 0, 1 or 2");

typedef struct {
    Sample samples[WINDOW];
    uint32_t epoch, timestamp;
} Block;

typedef struct {
    Features features;
    Condition state;
    float score;
    uint32_t cycles, timestamp, epoch, deadline_misses;
    unsigned trained;
} Report;

enum { POOL_SIZE = 3 };
static Block blocks[POOL_SIZE], raw;
static Sample window[WINDOW];
static unsigned filled;
static Detector detector;
static uint32_t previous_epoch, deadline_misses;
static volatile uint32_t epoch, spi_errors, overruns, dropped;
static volatile Condition fault;
static volatile bool capture_requested, capture_ready, learn_requested;
static QueueHandle_t available, ready, reports;
static StaticQueue_t queue_memory[3];
static uint8_t available_items[POOL_SIZE], ready_items[POOL_SIZE];
static uint8_t report_item[sizeof(Report)];
static StaticTask_t task_memory[3];
static StackType_t acquisition_stack[512], dsp_stack[1024], uart_stack[1024];

static void fault_set(Condition value)
{
    taskENTER_CRITICAL();
    fault = value;
    ++epoch;
    taskEXIT_CRITICAL();
    filled = 0;
}

static void acquire(void *unused)
{
    (void)unused;
    sensor_interrupts(xTaskGetCurrentTaskHandle());
    for (;;) {
        if (!sensor_init()) {
            ++spi_errors;
            fault_set(SENSOR_FAULT);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        unsigned empty = 0;
        for (;;) {
            /* Timeout also services a watermark already high when EXTI was enabled. */
            (void)ulTaskNotifyTakeIndexed(WATERMARK_EVENT, pdTRUE, pdMS_TO_TICKS(10));
            uint8_t source, count;
            if (!sensor_status(&source, &count)) {
                ++spi_errors;
                fault_set(SENSOR_FAULT);
                break;
            }
            if ((source & 1u) || count == 32) {
                ++overruns;
                fault_set(ACQUISITION_FAULT);
                break;
            }
            if (!count) {
                if (++empty < 5) continue;
                ++spi_errors;
                fault_set(SENSOR_FAULT);
                break;
            }
            empty = 0;
            Sample batch[32];
            bool ok = true;
            for (unsigned i = 0; i < count && ok; ++i) ok = sensor_read(&batch[i]);
            uint8_t after, remaining;
            if (!ok || !sensor_status(&after, &remaining)) {
                ++spi_errors;
                fault_set(SENSOR_FAULT);
                break;
            }
            if ((after & 1u) || remaining == 32) {
                ++overruns;
                fault_set(ACQUISITION_FAULT);
                break;
            }
            for (unsigned i = 0; i < count; ++i) {
                window[filled++] = batch[i];
                if (filled != WINDOW) continue;
                uint8_t index;
                if (xQueueReceive(available, &index, 0) != pdTRUE) {
                    ++dropped;
                    fault_set(ACQUISITION_FAULT);
                    break;
                }
                Block *block = &blocks[index];
                block->timestamp = xTaskGetTickCount();
                block->epoch = epoch;
                memcpy(block->samples, window, sizeof window);
                configASSERT(xQueueSend(ready, &index, 0) == pdTRUE);
                memmove(window, window + HOP, sizeof(Sample) * (WINDOW - HOP));
                filled = WINDOW - HOP;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void process(void *unused)
{
    (void)unused;
    configASSERT(analysis_init());
    for (;;) {
        uint8_t index;
        if (xQueueReceive(ready, &index, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        const Block *block = &blocks[index];
        if (block->epoch != epoch) {
            configASSERT(xQueueSend(available, &index, 0) == pdTRUE);
            continue;
        }
        uint32_t start = board_cycles();
        Report result = {.features = analyze(block->samples, ANALYSIS_AXIS), .timestamp = block->timestamp,
                         .epoch = block->epoch};
        Detector next = detector;
        if (block->epoch != previous_epoch) {
            next.raised = next.severe = next.shape = next.quiet = 0;
            if (next.count < TRAINING) next = (Detector){0};
        }
        bool relearn = learn_requested;
        if (relearn) next = (Detector){0};
        result.state = classify(&next, result.features, &result.score);
        result.trained = next.count;
        result.cycles = board_cycles() - start;
        if (xTaskGetTickCount() - block->timestamp >= HOP * 1000 / RATE) ++deadline_misses;
        result.deadline_misses = deadline_misses;
        bool capture = capture_requested && !capture_ready;
        if (capture) raw = *block;
        taskENTER_CRITICAL();
        if (result.epoch == epoch) {
            if (capture) {
                capture_requested = false;
                capture_ready = true;
            }
            if (relearn) learn_requested = false;
            previous_epoch = result.epoch;
            detector = next;
            fault = LEARNING;
            xQueueOverwrite(reports, &result);
        }
        taskEXIT_CRITICAL();
        configASSERT(xQueueSend(available, &index, 0) == pdTRUE);
    }
}

static void line(const char *payload)
{
    uint32_t crc = UINT32_MAX;
    for (const unsigned char *p = (const unsigned char *)payload; *p; ++p) {
        crc ^= *p;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    char trailer[12];
    snprintf(trailer, sizeof trailer, "*%08lx\n", (unsigned long)~crc);
    uart_send(payload);
    uart_send(trailer);
}

static long scaled(float value)
{
    /* Fixed-point telemetry avoids pulling in a floating-point printf heap. */
    float result = value * 1000000.0f;
    return (long)fmaxf(-2147483520.0f, fminf(2147483520.0f, result));
}

static void telemetry(void *unused)
{
    (void)unused;
    static const char *names[] = {"LEARNING", "NORMAL", "WARNING", "CRITICAL",
                                 "ANOMALY", "SENSOR_FAULT", "ACQUISITION_FAULT"};
    Report report = {0};
    uint32_t sequence = 0, last = 0, capture = 0;
    unsigned offset = WINDOW;
    for (;;) {
        int command = uart_receive();
        if (command == 'B') learn_requested = true;
        if (command == 'R' && !capture_ready) capture_requested = true;
        bool fresh = xQueueReceive(reports, &report, 0) == pdTRUE;
        Condition state = fault;
        bool valid = report.epoch == epoch && state == LEARNING;
        if (valid) state = report.state;
        board_led(state);
        uint32_t now = xTaskGetTickCount();
        char message[240];
        if (fresh || now - last >= 1000) {
            last = now;
            snprintf(message, sizeof message,
                     "F,%lu,%lu,%s,%lu,%lu,%lu,%lu,%lu,%u,%u,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
                     (unsigned long)sequence++, (unsigned long)(valid ? report.timestamp : now), names[state],
                     (unsigned long)(valid ? report.cycles : 0), (unsigned long)spi_errors,
                     (unsigned long)overruns, (unsigned long)dropped, (unsigned long)report.deadline_misses,
                     valid ? report.trained : 0u, (unsigned)ANALYSIS_AXIS, valid ? scaled(report.score) : 0L,
                     valid ? scaled(report.features.value[0]) : 0L, valid ? scaled(report.features.value[1]) : 0L,
                     valid ? scaled(report.features.value[2]) : 0L, valid ? scaled(report.features.value[3]) : 0L,
                     valid ? scaled(report.features.value[4]) : 0L, valid ? scaled(report.features.value[5]) : 0L,
                     valid ? scaled(report.features.value[6]) : 0L, valid ? scaled(report.features.value[7]) : 0L,
                     valid ? scaled(report.features.value[8]) : 0L, valid ? scaled(report.features.value[9]) : 0L);
            line(message);
        }
        if (capture_ready) {
            if (offset == WINDOW) {
                ++capture;
                offset = 0;
            }
            static const char digits[] = "0123456789abcdef";
            int length = snprintf(message, sizeof message, "R,%lu,%u,", (unsigned long)capture, offset);
            for (unsigned i = 0; i < 8; ++i) {
                for (unsigned axis = 0; axis < 3; ++axis) {
                    uint16_t word = (uint16_t)raw.samples[offset + i].axis[axis];
                    for (unsigned byte = 0; byte < 2; ++byte) {
                        unsigned value = (word >> (8 * byte)) & 255;
                        message[length++] = digits[value >> 4];
                        message[length++] = digits[value & 15];
                    }
                }
            }
            message[length] = '\0';
            line(message);
            offset += 8;
            if (offset == WINDOW) capture_ready = false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    board_led(CRITICAL);
    configASSERT(false);
}

int main(void)
{
    board_init();
    available = xQueueCreateStatic(POOL_SIZE, sizeof(uint8_t), available_items, &queue_memory[0]);
    ready = xQueueCreateStatic(POOL_SIZE, sizeof(uint8_t), ready_items, &queue_memory[1]);
    reports = xQueueCreateStatic(1, sizeof(Report), report_item, &queue_memory[2]);
    for (uint8_t i = 0; i < POOL_SIZE; ++i) configASSERT(xQueueSend(available, &i, 0) == pdTRUE);
    configASSERT(xTaskCreateStatic(acquire, "acquire", 512, NULL, 4, acquisition_stack, &task_memory[0]));
    configASSERT(xTaskCreateStatic(process, "analysis", 1024, NULL, 3, dsp_stack, &task_memory[1]));
    configASSERT(xTaskCreateStatic(telemetry, "telemetry", 1024, NULL, 1, uart_stack, &task_memory[2]));
    vTaskStartScheduler();
    for (;;) {}
}

void _init(void) {}
void _fini(void) {}

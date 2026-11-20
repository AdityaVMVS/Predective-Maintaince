#ifndef BOARD_H
#define BOARD_H
#include "analysis.h"
#include "FreeRTOS.h"
#include "task.h"

enum { WATERMARK_EVENT = 0, DMA_EVENT = 1 };
void board_init(void);
void sensor_interrupts(TaskHandle_t owner);
bool sensor_init(void);
bool sensor_status(uint8_t *source, uint8_t *count);
bool sensor_read(Sample *sample);
uint32_t board_cycles(void);
void board_led(Condition condition);
void uart_send(const char *text);
int uart_receive(void);
#endif

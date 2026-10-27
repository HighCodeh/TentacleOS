#ifndef TEST_TASK_H
#define TEST_TASK_H
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack,
                                  void *arg, unsigned priority, TaskHandle_t *out, int core);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout);
#endif

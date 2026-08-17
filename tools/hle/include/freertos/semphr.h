#pragma once

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial);

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken);
void vSemaphoreDelete(SemaphoreHandle_t sem);

// ESP-IDF recursive mutex extensions
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t timeout);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "FreeRTOSConfig.h"
#include "portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TimerHandle_t;
typedef void *EventGroupHandle_t;

typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;

typedef uint8_t StackType_t;
typedef struct { union { void *opaque[8]; double _align; }; } StaticTask_t;
typedef struct { union { void *opaque[8]; double _align; }; } StaticQueue_t;

#define pdFALSE  ((BaseType_t)0)
#define pdTRUE   ((BaseType_t)1)
#define pdPASS   pdTRUE
#define pdFAIL   pdFALSE

#define portMAX_DELAY    ((TickType_t)0xFFFFFFFF)
#define portTICK_PERIOD_MS 1

static inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms / portTICK_PERIOD_MS; }

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t func, const char *name, uint32_t stack_depth,
                                    void *params, UBaseType_t prio, TaskHandle_t *handle,
                                    BaseType_t core_id);
#define xTaskCreate(func, name, stack, params, prio, handle) \
    xTaskCreatePinnedToCore(func, name, stack, params, prio, handle, 0)

void vTaskDelete(TaskHandle_t handle);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
void vTaskSuspend(TaskHandle_t handle);
void vTaskResume(TaskHandle_t handle);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle);
char *pcTaskGetName(TaskHandle_t handle);
TaskHandle_t xTaskGetCurrentTaskHandle(void);

TaskHandle_t xTaskCreateStatic(TaskFunction_t func, const char *name, uint32_t stack_depth,
                                void *params, UBaseType_t prio, StackType_t *stack_buf,
                                StaticTask_t *task_buf);

#ifdef __cplusplus
}
#endif

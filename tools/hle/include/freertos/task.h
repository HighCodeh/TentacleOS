#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

#include <stdlib.h>
#include <string.h>
#include <sched.h>

#ifdef __cplusplus
extern "C" {
#endif

#define taskYIELD() sched_yield()

#define pvPortMalloc(size)  malloc(size)
#define vPortFree(p)        free(p)

typedef struct {
    TaskHandle_t xHandle;
    const char *pcTaskName;
    UBaseType_t uxCurrentPriority;
    uint32_t usStackHighWaterMark;
} TaskStatus_t;

UBaseType_t uxTaskGetNumberOfTasks(void);
void        vTaskPrioritySet(TaskHandle_t handle, UBaseType_t prio);
void        vTaskList(char *buffer);
void        vTaskGetRunTimeStats(char *buffer);

#ifdef __cplusplus
}
#endif

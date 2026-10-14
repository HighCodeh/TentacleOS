#pragma once

#include "freertos/FreeRTOS.h"

static inline BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t func, const char *name,
                                                         uint32_t stack_depth, void *params,
                                                         UBaseType_t prio, TaskHandle_t *handle,
                                                         BaseType_t core_id, uint32_t caps) {
    (void)caps;
    return xTaskCreatePinnedToCore(func, name, stack_depth, params, prio, handle, core_id);
}

static inline void vTaskDeleteWithCaps(TaskHandle_t handle) { vTaskDelete(handle); }

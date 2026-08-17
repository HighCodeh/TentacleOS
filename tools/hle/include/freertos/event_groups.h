#pragma once

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t eg, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg, EventBits_t bits, BaseType_t clear,
                                  BaseType_t wait_all, TickType_t timeout);

#ifdef __cplusplus
}
#endif

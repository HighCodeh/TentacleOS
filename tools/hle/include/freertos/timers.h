#pragma once

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

TimerHandle_t xTimerCreate(const char *name, TickType_t period, UBaseType_t auto_reload,
                            void *timer_id, void (*callback)(TimerHandle_t));
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t timeout);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t timeout);
BaseType_t xTimerReset(TimerHandle_t timer, TickType_t timeout);

#ifdef __cplusplus
}
#endif

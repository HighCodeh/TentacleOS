#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define configMINIMAL_STACK_SIZE 1024
#define configTICK_RATE_HZ       1000
#define configMAX_PRIORITIES     25
#define configUSE_PREEMPTION     1
#define configUSE_TIME_SLICING   1
#define portSHORT short
#define tskIDLE_PRIORITY 0

#ifdef __cplusplus
}
#endif

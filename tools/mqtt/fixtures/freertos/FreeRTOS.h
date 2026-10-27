#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <pthread.h>
#include <stdint.h>
typedef pthread_mutex_t portMUX_TYPE;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) pthread_mutex_lock(lock)
#define portEXIT_CRITICAL(lock) pthread_mutex_unlock(lock)
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#endif

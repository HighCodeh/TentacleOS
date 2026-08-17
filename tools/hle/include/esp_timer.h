#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t esp_timer_get_time(void);

typedef struct {
    const char *name;
    void (*callback)(void *arg);
    void *arg;
    uint64_t alarm;
    bool dispatch;
} esp_timer_create_args_t;

typedef void *esp_timer_handle_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t handle);
esp_err_t esp_timer_delete(esp_timer_handle_t handle);

#ifdef __cplusplus
}
#endif

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void esp_restart(void);

typedef enum {
    ESP_RST_UNKNOWN,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO,
    ESP_RST_USB,
    ESP_RST_JTAG,
    ESP_RST_EFUSE,
    ESP_RST_PWR_GLITCH,
    ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;

esp_reset_reason_t esp_reset_reason(void);

typedef void (*shutdown_handler_t)(void);
esp_err_t esp_register_shutdown_handler(shutdown_handler_t handle);
uint32_t esp_get_minimum_free_heap_size(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <inttypes.h>

/* Transitive includes mirroring ESP-IDF behavior: real esp_log.h indirectly
 * exposes heap_caps and inttypes through its dependency chain. Firmware source
 * files rely on this without explicit includes. */
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LOG_NONE    0
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E [%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W [%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I [%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) fprintf(stderr, "D [%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) fprintf(stderr, "V [%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buf, len, level) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(tag, buf, len, level) ((void)0)

typedef int (*vprintf_like_t)(const char *fmt, va_list ap);

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);

#ifdef __cplusplus
}
#endif

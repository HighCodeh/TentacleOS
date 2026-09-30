#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t esp_random(void) {
    return (uint32_t)rand();
}

static inline void esp_fill_random(void *buf, size_t len) {
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)esp_random();
}

#ifdef __cplusplus
}
#endif

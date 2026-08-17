#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t esp_random(void) {
    return (uint32_t)rand();
}

#ifdef __cplusplus
}
#endif

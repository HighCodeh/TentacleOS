#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void ets_delay_us(uint32_t us) { (void)us; }

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    void *p = malloc(size);
    if (p) memset(p, 0, size);
    return p;
}

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    (void)alignment; (void)caps;
    void *p = malloc(size);
    if (p) memset(p, 0, size);
    return p;
}

static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 8 * 1024 * 1024; }

#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_8BIT   0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA    0
#define MALLOC_CAP_INTERNAL 0

#ifdef __cplusplus
}
#endif

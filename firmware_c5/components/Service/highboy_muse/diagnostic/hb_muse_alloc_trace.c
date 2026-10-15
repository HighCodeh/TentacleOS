/* Diagnostic only: bounded, allocation-free caller evidence after failure.
 * --wrap preserves the real allocator, capabilities and return value. */
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#ifdef HB_ALLOC_TRACE_TEST
#include "alloc_trace_test.h"
#else
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#endif

void *__real_heap_caps_malloc(size_t size, uint32_t caps);
void *__real_heap_caps_calloc(size_t count, size_t size, uint32_t caps);
void *__real_bt_osi_mem_malloc_internal(size_t size);
void *__real_bt_osi_mem_calloc_internal(size_t count, size_t size);
static atomic_uint failures;

static void record_failure(size_t count, size_t size, uint32_t caps, void *caller) {
  const uint32_t dma = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA;
  if ((caps & dma) != dma)
    return;
  unsigned n = atomic_load_explicit(&failures, memory_order_relaxed);
  do {
    if (n >= 8)
      return;
  } while (!atomic_compare_exchange_weak_explicit(
      &failures, &n, n + 1, memory_order_relaxed, memory_order_relaxed));
  /* ROM output avoids the logging hook, queue and formatting allocations.
   * Caller is a code address only. Never inspect memory or payload bytes. */
  esp_rom_printf("MUSE_ALLOC n=%u count=%u size=%u caps=0x%x pc=%p dma=%u largest=%u\n",
                 n + 1,
                 (unsigned)count,
                 (unsigned)size,
                 (unsigned)caps,
                 caller,
                 (unsigned)heap_caps_get_free_size(dma),
                 (unsigned)heap_caps_get_largest_free_block(dma));
}

void *__wrap_heap_caps_malloc(size_t size, uint32_t caps) {
  void *result = __real_heap_caps_malloc(size, caps);
  if (!result)
    record_failure(1, size, caps, __builtin_return_address(0));
  return result;
}

void *__wrap_heap_caps_calloc(size_t count, size_t size, uint32_t caps) {
  void *result = __real_heap_caps_calloc(count, size, caps);
  if (!result)
    record_failure(count, size, caps, __builtin_return_address(0));
  return result;
}

/* Capture the caller above the controller allocator as well as the generic
 * heap caller. Both share the eight-line cap and preserve return values. */
void *__wrap_bt_osi_mem_malloc_internal(size_t size) {
  void *result = __real_bt_osi_mem_malloc_internal(size);
  if (!result)
    record_failure(1, size, 0x80c, __builtin_return_address(0));
  return result;
}
void *__wrap_bt_osi_mem_calloc_internal(size_t count, size_t size) {
  void *result = __real_bt_osi_mem_calloc_internal(count, size);
  if (!result)
    record_failure(count, size, 0x80c, __builtin_return_address(0));
  return result;
}

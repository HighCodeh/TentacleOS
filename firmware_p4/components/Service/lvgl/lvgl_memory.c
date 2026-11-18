// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

/* UI allocations must not compete with the P4 radio/crypto DMA heap. */
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include <string.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
#if CONFIG_SPIRAM
#define UI_MEMORY_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define UI_MEMORY_CAPS MALLOC_CAP_8BIT
#endif

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *memory, size_t bytes) {
  (void)memory;
  (void)bytes;
  return NULL;
}
void lv_mem_remove_pool(lv_mem_pool_t pool) {
  (void)pool;
}
void *lv_malloc_core(size_t bytes) {
  return heap_caps_malloc(bytes, UI_MEMORY_CAPS);
}
void *lv_realloc_core(void *memory, size_t bytes) {
  return heap_caps_realloc(memory, bytes, UI_MEMORY_CAPS);
}
void lv_free_core(void *memory) {
  heap_caps_free(memory);
}
void lv_mem_monitor_core(lv_mem_monitor_t *monitor) {
  /* Like CLIB, this backend has no private LVGL pool to report. */
  memset(monitor, 0, sizeof(*monitor));
}
lv_result_t lv_mem_test_core(void) {
  return heap_caps_check_integrity(UI_MEMORY_CAPS, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}
#endif

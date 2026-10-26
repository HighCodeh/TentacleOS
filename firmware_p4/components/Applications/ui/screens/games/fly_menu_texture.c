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

#include "fly_menu_texture.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "menu_ui.h"
#include "others/snapshot/lv_snapshot.h"

#define SOURCE_WIDTH  240U
#define SOURCE_HEIGHT 320U
#define SOURCE_STRIDE (SOURCE_WIDTH * sizeof(uint16_t))
#define SOURCE_BYTES  (SOURCE_STRIDE * SOURCE_HEIGHT)

static menu_ui_preview_t *s_preview;
static void *s_scratch;
static uint16_t *s_pixels;
static lv_draw_buf_t s_draw;
static uint32_t s_elapsed;
static unsigned s_selected;
static bool s_valid;

bool fly_menu_texture_downsample(const uint16_t *source,
                                 size_t source_capacity,
                                 unsigned width,
                                 unsigned height,
                                 unsigned stride,
                                 uint16_t *destination,
                                 size_t destination_capacity) {
  if (source == NULL || destination == NULL || width != SOURCE_WIDTH || height != SOURCE_HEIGHT ||
      stride < width || stride > SOURCE_WIDTH + 32U || source_capacity < (size_t)stride * height ||
      destination_capacity < FLY_MENU_TEXTURE_PIXELS)
    return false;
  uintptr_t from = (uintptr_t)source, to = (uintptr_t)destination;
  size_t from_bytes = (size_t)stride * height * sizeof(uint16_t);
  size_t to_bytes = FLY_MENU_TEXTURE_PIXELS * sizeof(uint16_t);
  if (from == to || (from < to && to - from < from_bytes) || (to < from && from - to < to_bytes))
    return false;
  // Center sample preserves the exact RGB565 palette; no fabricated overlay.
  for (unsigned y = 0; y < FLY_MENU_TEXTURE_HEIGHT; ++y) {
    unsigned sy = ((2U * y + 1U) * height) / (2U * FLY_MENU_TEXTURE_HEIGHT);
    for (unsigned x = 0; x < FLY_MENU_TEXTURE_WIDTH; ++x) {
      unsigned sx = ((2U * x + 1U) * width) / (2U * FLY_MENU_TEXTURE_WIDTH);
      destination[y * FLY_MENU_TEXTURE_WIDTH + x] = source[sy * stride + sx];
    }
  }
  return true;
}

void fly_menu_texture_close(void) {
  s_valid = false;
  if (s_preview != NULL)
    menu_ui_preview_destroy(s_preview);
  s_preview = NULL;
  if (s_scratch != NULL)
    heap_caps_free(s_scratch);
  if (s_pixels != NULL)
    heap_caps_free(s_pixels);
  s_scratch = NULL;
  s_pixels = NULL;
  memset(&s_draw, 0, sizeof(s_draw));
  s_elapsed = 0;
  s_selected = 0;
}

static bool capture(void) {
  s_valid = false;
  lv_obj_t *root = menu_ui_preview_root(s_preview);
  if (root == NULL || s_pixels == NULL)
    return false;
  if (lv_snapshot_take_to_draw_buf(root, LV_COLOR_FORMAT_RGB565, &s_draw) != LV_RESULT_OK)
    return false;
  if (s_draw.header.cf != LV_COLOR_FORMAT_RGB565 || (s_draw.header.stride & 1U) != 0)
    return false;
  s_valid = fly_menu_texture_downsample((const uint16_t *)s_draw.data,
                                        s_draw.data_size / sizeof(uint16_t),
                                        s_draw.header.w,
                                        s_draw.header.h,
                                        s_draw.header.stride / sizeof(uint16_t),
                                        s_pixels,
                                        FLY_MENU_TEXTURE_PIXELS);
  return s_valid;
}

bool fly_menu_texture_open(void) {
  fly_menu_texture_close();
  s_scratch =
      heap_caps_malloc(SOURCE_BYTES + LV_DRAW_BUF_ALIGN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_pixels = heap_caps_malloc(FLY_MENU_TEXTURE_PIXELS * sizeof(uint16_t),
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_scratch == NULL || s_pixels == NULL)
    goto failed;
  memset(s_pixels, 0, FLY_MENU_TEXTURE_PIXELS * sizeof(uint16_t));
  if (lv_draw_buf_init(&s_draw,
                       SOURCE_WIDTH,
                       SOURCE_HEIGHT,
                       LV_COLOR_FORMAT_RGB565,
                       SOURCE_STRIDE,
                       s_scratch,
                       SOURCE_BYTES + LV_DRAW_BUF_ALIGN) != LV_RESULT_OK)
    goto failed;
  s_preview = menu_ui_preview_create();
  if (s_preview == NULL || !capture())
    goto failed;
  return true;
failed:
  fly_menu_texture_close();
  return false;
}

bool fly_menu_texture_tick(uint32_t elapsed_ms) {
  if (s_preview == NULL || s_pixels == NULL)
    return false;
  // A long suspended UI advances at most one native catalog item, not a burst.
  uint32_t step =
      elapsed_ms > FLY_MENU_TEXTURE_INTERVAL_MS ? FLY_MENU_TEXTURE_INTERVAL_MS : elapsed_ms;
  s_elapsed += step;
  if (s_elapsed < FLY_MENU_TEXTURE_INTERVAL_MS)
    return false;
  s_elapsed %= FLY_MENU_TEXTURE_INTERVAL_MS;
  int total = menu_catalog_count();
  if (total <= 0) {
    s_valid = false;
    return false;
  }
  s_selected = (s_selected + 1U) % (unsigned)total;
  if (!menu_ui_preview_select(s_preview, s_selected)) {
    s_valid = false;
    return false;
  }
  return capture();
}

const uint16_t *fly_menu_texture_pixels(void) {
  return s_valid ? s_pixels : NULL;
}

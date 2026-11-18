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

#include "ui_chrome.h"
#include "misc/cache/instance/lv_image_cache.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "st7789.h"

#include "header_ui.h"
#include "ui_metrics.h"
#include "ui_theme.h"

// Keep this shared setup out of individual screen builders: many routes use
// the same root style, and repeating it wastes scarce OTA application space.
__attribute__((noinline)) lv_obj_t *ui_chrome_screen_create(void) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  return screen;
}

// Whether the next chrome header shows the breadcrumb letreiro. Set per screen by
// ui_chrome_set_status_enabled(): browse screens = true, active operation screens
// (reading/sending/scanning/emulating/...) = false. The status HEADER itself is
// drawn on EVERY screen regardless; only the letreiro (and the dropdown) are gated.
static bool s_status_enabled = true;

void ui_chrome_set_status_enabled(bool enabled) {
  s_status_enabled = enabled;
}

bool ui_chrome_status_enabled(void) {
  return s_status_enabled;
}

// Breadcrumb root: the most-recent menu's title. Leaf screens show "root / title";
// menus set it (showing just their own name); home/coverflow clear it.
static char s_bc_root[24] = "";

void ui_chrome_set_breadcrumb_root(const char *root) {
  snprintf(s_bc_root, sizeof(s_bc_root), "%s", root ? root : "");
}

void ui_chrome_compose_title(char *dst, size_t n, const char *title) {
  if (!dst || n == 0)
    return;
  const char *t = title ? title : "";
  if (s_bc_root[0] && strcmp(s_bc_root, t) != 0)
    snprintf(dst, n, "%s / %s", s_bc_root, t);
  else
    snprintf(dst, n, "%s", t);
}

lv_obj_t *ui_chrome_light_title(lv_obj_t *container, const char *title) {
  if (!container)
    return NULL;
  // The dynamic status header, IDENTICAL to home/coverflow (floating card: 12:00 +
  // wifi/bt/sd/battery), on EVERY screen. Sits at the very top so its -12 float
  // clips against the display edge exactly as on home, and it (re)binds the shared
  // status statics + singleton timers.
  header_ui_create(container);
  // The breadcrumb light label rides just below the card ONLY on browse screens;
  // active operation screens show the header alone, no letreiro.
  if (!s_status_enabled)
    return NULL;
  lv_obj_t *lbl = lv_label_create(container);
  lv_label_set_text(lbl, title ? title : "");
  lv_obj_set_style_text_color(lbl, current_theme.border_accent, 0);
  lv_obj_set_style_text_opa(lbl, LV_OPA_80, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl, ui_screen_w() - 24);
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 12, -1);
  return lbl;
}

// Transparent top-area container of the standard header height, so screen content
// still starts at UI_CHROME_HEADER_H and no per-screen layout changes are needed.
static lv_obj_t *make_header_container(lv_obj_t *parent) {
  lv_obj_t *hdr = lv_obj_create(parent);
  lv_obj_set_size(hdr, lv_pct(100), UI_CHROME_HEADER_H);
  lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  return hdr;
}

lv_obj_t *ui_chrome_header(lv_obj_t *parent, const char *title, const char *icon_path) {
  (void)icon_path; // the shared status header carries its own icons now
  char composed[48];
  ui_chrome_compose_title(composed, sizeof(composed), title);
  lv_obj_t *hdr = make_header_container(parent);
  ui_chrome_light_title(hdr, composed);
  return hdr;
}

lv_obj_t *ui_chrome_header_overlay(lv_obj_t *parent, const char *title, const char *icon_path) {
  (void)title;
  (void)icon_path;
  // Transient overlay drawn over a live screen: a STATIC snapshot header (no
  // globals, no timers, no letreiro), so tearing the overlay down never dangles
  // the dynamic header of the screen underneath.
  lv_obj_t *hdr = make_header_container(parent);
  header_ui_create_snapshot(hdr);
  return hdr;
}

lv_obj_t *ui_chrome_footer(lv_obj_t *parent, const char *hint) {
  lv_obj_t *ft = lv_obj_create(parent);
  lv_obj_set_size(ft, lv_pct(100), UI_CHROME_FOOTER_H);
  lv_obj_align(ft, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_remove_flag(ft, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(ft, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(ft, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(ft, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ft, 0, 0);
  lv_obj_set_style_pad_all(ft, 0, 0);
  lv_obj_set_style_border_width(ft, 1, 0);
  lv_obj_set_style_border_color(ft, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(ft, LV_BORDER_SIDE_TOP, 0);

  lv_obj_t *lbl = lv_label_create(ft);
  lv_obj_set_width(lbl, ui_screen_w() - 12);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl, hint ? hint : "");
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(lbl, LV_OPA_90, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(lbl);

  return ft;
}

void ui_chrome_footer_set_text(lv_obj_t *footer, const char *hint) {
  if (!footer)
    return;
  uint32_t n = lv_obj_get_child_count(footer);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *c = lv_obj_get_child(footer, i);
    if (lv_obj_check_type(c, &lv_label_class)) {
      lv_label_set_text(c, hint ? hint : "");
      return;
    }
  }
}

#define GRADIENT_TILE_W 32
#define GRADIENT_MAX_H  320
#define DITHER_LEVELS   64

static const uint8_t DITHER_8X8[DITHER_LEVELS] = {
    0,  48, 12, 60, 3,  51, 15, 63, 32, 16, 44, 28, 35, 19, 47, 31, 8,  56, 4,  52, 11, 59,
    7,  55, 40, 24, 36, 20, 43, 27, 39, 23, 2,  50, 14, 62, 1,  49, 13, 61, 34, 18, 46, 30,
    33, 17, 45, 29, 10, 58, 6,  54, 9,  57, 5,  53, 42, 26, 38, 22, 41, 25, 37, 21,
};

typedef struct {
  lv_image_dsc_t image;
  uint16_t pixels[];
} ui_gradient_t;

// Interpolate before quantization, retaining six fractional bits per channel.
static uint32_t gradient_channel(uint8_t top, uint8_t bottom, int y, int height) {
  int span = height > 1 ? height - 1 : 1;
  return ((uint32_t)top * (span - y) + (uint32_t)bottom * y) * DITHER_LEVELS / span;
}

static void gradient_fill(uint16_t *pixels, int height, lv_color_t top, lv_color_t bottom) {
  for (int y = 0; y < height; ++y) {
    uint32_t red = gradient_channel(top.red, bottom.red, y, height) * 31 / 255;
    uint32_t green = gradient_channel(top.green, bottom.green, y, height) * 63 / 255;
    uint32_t blue = gradient_channel(top.blue, bottom.blue, y, height) * 31 / 255;
    for (int x = 0; x < GRADIENT_TILE_W; ++x) {
      unsigned threshold = DITHER_LEVELS - 1 - DITHER_8X8[(y % 8) * 8 + x % 8];
      pixels[y * GRADIENT_TILE_W + x] = (((red + threshold) / DITHER_LEVELS) << 11) |
                                        (((green + threshold) / DITHER_LEVELS) << 5) |
                                        ((blue + threshold) / DITHER_LEVELS);
    }
  }
}

static void gradient_delete(lv_event_t *event) {
  ui_gradient_t *gradient = lv_event_get_user_data(event);
  lv_image_cache_drop(&gradient->image);
  free(gradient);
}

esp_err_t ui_chrome_gradient(lv_obj_t *obj, lv_color_t top, lv_color_t bottom, int height) {
  if (obj == NULL || height < 1 || height > GRADIENT_MAX_H)
    return ESP_ERR_INVALID_ARG;
  lv_obj_set_style_bg_color(obj, top, 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  if (lv_color_eq(top, bottom))
    return ESP_OK;
  size_t size = GRADIENT_TILE_W * height * sizeof(uint16_t);
  ui_gradient_t *gradient =
      heap_caps_calloc(1, sizeof(*gradient) + size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (gradient == NULL) {
    ESP_LOGE("UI_CHROME", "No PSRAM for gradient; using solid background");
    return ESP_ERR_NO_MEM;
  }
  gradient_fill(gradient->pixels, height, top, bottom);
  gradient->image.header.magic = LV_IMAGE_HEADER_MAGIC;
  gradient->image.header.cf = LV_COLOR_FORMAT_RGB565;
  gradient->image.header.w = GRADIENT_TILE_W;
  gradient->image.header.h = height;
  gradient->image.header.stride = GRADIENT_TILE_W * sizeof(uint16_t);
  gradient->image.data_size = size;
  gradient->image.data = (const uint8_t *)gradient->pixels;
  if (lv_obj_add_event_cb(obj, gradient_delete, LV_EVENT_DELETE, gradient) == NULL) {
    ESP_LOGE("UI_CHROME", "Cannot register gradient cleanup; using solid background");
    free(gradient);
    return ESP_ERR_NO_MEM;
  }
  lv_obj_set_style_bg_image_tiled(obj, true, 0);
  lv_obj_set_style_bg_image_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_image_src(obj, &gradient->image, 0);
  return ESP_OK;
}

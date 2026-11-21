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

#include "psk_qr_ui.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "src/libs/qrcode/qrcodegen.h"
#include "src/misc/cache/lv_cache.h"

#include "host_link_sec.h"
#include "led_control.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define QR_VERSION       5
#define QR_QUIET_MODULES 4
#define QR_MAX_SCALE     4
#define QR_MIN_SCALE     3
#define QR_BITS_PER_BYTE 8
#define VIEW_PADDING     6
#define LABEL_HEIGHT     16
#define LABEL_GAP        2
#define KEY_ROW_LENGTH   16
#define KEY_ROWS         ((HOST_LINK_PSK_HEX_SIZE - 1) / KEY_ROW_LENGTH)
#define CAPTION_INDEX    0
#define QR_SLOT_INDEX    1
#define KEY_INDEX        2
#define STATUS_INDEX     3

typedef struct {
  uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
  uint8_t symbol[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
} psk_qr_workspace_t;

static const char *TAG = "PSK_QR_UI";

static void release_pixels(lv_event_t *event);
static bool create_qr(lv_obj_t *slot, const char *psk, int32_t available);
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color);

lv_obj_t *psk_qr_ui_create(lv_obj_t *parent, const char *psk, const char *status) {
  lv_obj_t *view = lv_obj_create(parent);
  lv_obj_set_size(view, lv_pct(100), lv_pct(100));
  lv_obj_remove_flag(view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(view, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(view, 0, 0);
  lv_obj_set_style_pad_all(view, 0, 0);
  lv_obj_update_layout(view);
  int32_t width = lv_obj_get_content_width(view) - VIEW_PADDING * 2;
  int32_t height = lv_obj_get_content_height(view) - (LABEL_HEIGHT + LABEL_GAP) * 2;
  lv_obj_t *caption = make_label(view, "Scan PSK to connect", current_theme.text_main);
  lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t *slot = lv_obj_create(view);
  lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(slot, 0, 0);
  lv_obj_set_style_pad_all(slot, 0, 0);
  bool has_psk = psk != NULL && strlen(psk) == HOST_LINK_PSK_HEX_SIZE - 1;
  char key_text[HOST_LINK_PSK_HEX_SIZE + KEY_ROWS];
  if (has_psk) {
    size_t offset = 0;
    for (size_t i = 0; i < HOST_LINK_PSK_HEX_SIZE - 1; ++i) {
      if (i != 0 && i % KEY_ROW_LENGTH == 0)
        key_text[offset++] = '\n';
      key_text[offset++] = psk[i];
    }
    key_text[offset] = '\0';
  }
  lv_obj_t *key = make_label(view, has_psk ? key_text : "PSK unavailable", current_theme.text_main);
  lv_obj_set_width(key, width);
  lv_obj_center(key);
  lv_obj_t *state = make_label(view, status, current_theme.text_secondary);
  lv_obj_align(state, LV_ALIGN_BOTTOM_MID, 0, 0);
  if (has_psk && create_qr(slot, psk, LV_MIN(width, height))) {
    lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(slot, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(caption, has_psk ? "QR unavailable - use PSK" : "Connection key unavailable");
    if (!has_psk)
      ESP_LOGE(TAG, "PSK unavailable for display");
    led_signal_error();
  }
  return view;
}

void psk_qr_ui_toggle_key(lv_obj_t *view) {
  if (view == NULL)
    return;
  lv_obj_t *slot = lv_obj_get_child(view, QR_SLOT_INDEX);
  if (lv_obj_get_child_count(slot) == 0)
    return;
  bool is_key_visible = lv_obj_has_flag(slot, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flag(slot, LV_OBJ_FLAG_HIDDEN, !is_key_visible);
  lv_obj_set_flag(lv_obj_get_child(view, KEY_INDEX), LV_OBJ_FLAG_HIDDEN, is_key_visible);
  lv_label_set_text(lv_obj_get_child(view, CAPTION_INDEX),
                    is_key_visible ? "Scan PSK to connect" : "Pairing key (PSK)");
}

void psk_qr_ui_set_status(lv_obj_t *view, const char *status) {
  if (view != NULL)
    lv_label_set_text(lv_obj_get_child(view, STATUS_INDEX), status);
}

static void release_pixels(lv_event_t *event) {
  lv_image_cache_drop(lv_canvas_get_draw_buf(lv_event_get_target(event)));
  free(lv_event_get_user_data(event));
}

static bool create_qr(lv_obj_t *slot, const char *psk, int32_t available) {
  bool is_ready = false;
  uint8_t *pixels = NULL;
  lv_obj_t *canvas = NULL;
  psk_qr_workspace_t *workspace =
      heap_caps_malloc(sizeof(*workspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (workspace == NULL) {
    ESP_LOGE(TAG, "Cannot allocate QR workspace");
    goto cleanup;
  }
  memcpy(workspace->scratch, psk, HOST_LINK_PSK_HEX_SIZE - 1);
  if (!qrcodegen_encodeBinary(workspace->scratch,
                              HOST_LINK_PSK_HEX_SIZE - 1,
                              workspace->symbol,
                              qrcodegen_Ecc_MEDIUM,
                              QR_VERSION,
                              QR_VERSION,
                              qrcodegen_Mask_AUTO,
                              true)) {
    ESP_LOGE(TAG, "Cannot encode PSK QR");
    goto cleanup;
  }
  int32_t modules = qrcodegen_getSize(workspace->symbol) + QR_QUIET_MODULES * 2;
  int32_t scale = LV_MIN(available / modules, QR_MAX_SCALE);
  if (scale < QR_MIN_SCALE) {
    ESP_LOGE(TAG, "Not enough room for a readable QR");
    goto cleanup;
  }
  int32_t size = modules * scale;
  size_t buffer_size = LV_DRAW_BUF_SIZE(size, size, LV_COLOR_FORMAT_I1);
  pixels = heap_caps_calloc(1, buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pixels == NULL) {
    ESP_LOGE(TAG, "Cannot allocate QR pixels");
    goto cleanup;
  }
  canvas = lv_canvas_create(slot);
  if (canvas == NULL) {
    ESP_LOGE(TAG, "Cannot create QR canvas");
    goto cleanup;
  }
  lv_canvas_set_buffer(canvas, pixels, size, size, LV_COLOR_FORMAT_I1);
  lv_draw_buf_t *draw = lv_canvas_get_draw_buf(canvas);
  draw->data_size = buffer_size;
  lv_canvas_set_palette(canvas, 0, lv_color_to_32(lv_color_hex(UI_COL_QR_LIGHT), LV_OPA_COVER));
  lv_canvas_set_palette(canvas, 1, lv_color_to_32(lv_color_hex(UI_COL_QR_DARK), LV_OPA_COVER));
  uint8_t *bitmap =
      pixels + LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) * sizeof(lv_color32_t);
  for (int32_t y = 0; y < size; ++y) {
    for (int32_t x = 0; x < size; ++x) {
      if (qrcodegen_getModule(
              workspace->symbol, x / scale - QR_QUIET_MODULES, y / scale - QR_QUIET_MODULES))
        bitmap[y * draw->header.stride + x / QR_BITS_PER_BYTE] |=
            1U << (QR_BITS_PER_BYTE - 1 - x % QR_BITS_PER_BYTE);
    }
  }
  lv_obj_set_size(slot, size, size);
  lv_obj_center(slot);
  lv_obj_center(canvas);
  if (lv_obj_add_event_cb(canvas, release_pixels, LV_EVENT_DELETE, pixels) == NULL) {
    ESP_LOGE(TAG, "Cannot register QR cleanup");
    goto cleanup;
  }
  pixels = NULL;
  is_ready = true;
cleanup:
  if (!is_ready && canvas != NULL)
    lv_obj_delete(canvas);
  free(pixels);
  free(workspace);
  return is_ready;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label, text);
  return label;
}

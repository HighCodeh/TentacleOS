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

#include "ble_sniffer_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#include "ble_sniffer.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "BLE_SNIFFER_UI";

#define FEED_TICK_MS 200

#define MAX_ROWS     7
#define ROW_LEN      40
#define FEED_BUF_LEN (MAX_ROWS * ROW_LEN)

#define COL_DIM 0x8A8594

#define SNIFF_ICON "/assets/icons/monitoring.bin"

#define STATUS_Y      48
#define FRAMES_CARD_W 150
#define FRAMES_CARD_H 34
#define FRAMES_CARD_Y 70
#define HEX_PANEL_W   224
#define HEX_PANEL_H   150
#define HEX_PANEL_Y   112
#define HEX_PANEL_PAD 8

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_feed_timer = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_hex_label = NULL;

static char s_rows[MAX_ROWS][ROW_LEN];
static int s_row_count = 0;
static uint32_t s_frames = 0;
static uint32_t s_rendered_frames = 0;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void ble_sniffer_input(const input_event_t *ev, void *ctx);
static void feed_tick_cb(lv_timer_t *timer);

static void stop_sniffing(void) {
  ble_sniffer_set_observer(NULL);
  ble_sniffer_stop();
  if (s_feed_timer != NULL) {
    lv_timer_delete(s_feed_timer);
    s_feed_timer = NULL;
  }
}

static void sniffer_observer(const ble_sniffer_adv_t *adv) {
  const uint8_t *addr = adv->addr;
  char row[ROW_LEN];
  int n = snprintf(row,
                   sizeof(row),
                   "%02X:%02X:%02X:%02X:%02X:%02X %d",
                   addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], adv->rssi);
  for (int i = 0; i < adv->len && n > 0 && n < (int)sizeof(row) - 3; i++)
    n += snprintf(row + n, sizeof(row) - n, " %02X", adv->data[i]);

  portENTER_CRITICAL(&s_lock);
  if (s_row_count < MAX_ROWS) {
    memcpy(s_rows[s_row_count], row, sizeof(row));
    s_row_count++;
  } else {
    for (int i = 1; i < MAX_ROWS; i++)
      memcpy(s_rows[i - 1], s_rows[i], ROW_LEN);
    memcpy(s_rows[MAX_ROWS - 1], row, sizeof(row));
  }
  s_frames++;
  portEXIT_CRITICAL(&s_lock);
}

static lv_obj_t *lit_card(lv_obj_t *parent, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);
  return card;
}

static void render_rows(void) {
  if (s_hex_label == NULL)
    return;
  char rows[MAX_ROWS][ROW_LEN];
  int count;
  portENTER_CRITICAL(&s_lock);
  count = s_row_count;
  memcpy(rows, s_rows, sizeof(rows));
  portEXIT_CRITICAL(&s_lock);

  char buf[FEED_BUF_LEN];
  size_t pos = 0;
  for (int i = 0; i < count && pos < sizeof(buf); i++) {
    int m = snprintf(buf + pos, sizeof(buf) - pos, (i == 0) ? "%s" : "\n%s", rows[i]);
    if (m < 0)
      break;
    pos += (size_t)m;
  }
  if (count == 0)
    snprintf(buf, sizeof(buf), "Waiting for frames...");
  lv_label_set_text(s_hex_label, buf);
}

void ui_ble_sniffer_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_feed_timer = NULL;
  s_count_label = NULL;
  s_hex_label = NULL;
  portENTER_CRITICAL(&s_lock);
  s_row_count = 0;
  s_frames = 0;
  portEXIT_CRITICAL(&s_lock);
  s_rendered_frames = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "SNIFFER", SNIFF_ICON);
  ui_chrome_footer(s_screen, "BACK Stop");

  lv_obj_t *status = lv_label_create(s_screen);
  lv_label_set_text(status, "Sniffing...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  lv_obj_t *card = lit_card(s_screen, FRAMES_CARD_W, FRAMES_CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, FRAMES_CARD_Y);

  s_count_label = lv_label_create(card);
  lv_label_set_text(s_count_label, "Frames: 0");
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_center(s_count_label);

  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_set_size(panel, HEX_PANEL_W, HEX_PANEL_H);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, HEX_PANEL_Y);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(panel, 10, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(panel, HEX_PANEL_PAD, 0);

  s_hex_label = lv_label_create(panel);
  lv_label_set_text(s_hex_label, "");
  lv_obj_set_style_text_color(s_hex_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_hex_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_hex_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(s_hex_label, LV_ALIGN_TOP_LEFT, 0, 0);

  ui_input_set_screen_handler(ble_sniffer_input, NULL);

  ble_sniffer_set_observer(sniffer_observer);
  esp_err_t err = ble_sniffer_start();
  if (err != ESP_OK) {
    ble_sniffer_set_observer(NULL);
    lv_label_set_text(status, "Radio unavailable");
    lv_label_set_text(s_hex_label, "Could not start sniffer (C5?)");
    ESP_LOGE(TAG, "ble_sniffer_start failed: %s", esp_err_to_name(err));
  } else {
    s_feed_timer = lv_timer_create(feed_tick_cb, FEED_TICK_MS, NULL);
  }

  ui_feedback(UI_FB_SELECT);
  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "BLE sniffer screen opened");
}

static void feed_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    stop_sniffing();
    return;
  }

  uint32_t frames;
  portENTER_CRITICAL(&s_lock);
  frames = s_frames;
  portEXIT_CRITICAL(&s_lock);

  if (frames == s_rendered_frames)
    return;
  s_rendered_frames = frames;

  render_rows();
  if (s_count_label != NULL)
    lv_label_set_text_fmt(s_count_label, "Frames: %lu", (unsigned long)frames);
}

static void ble_sniffer_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        stop_sniffing();
        ui_switch_screen(SCREEN_BLE_DETECT_MENU);
      }
      break;
    default:
      break;
  }
}

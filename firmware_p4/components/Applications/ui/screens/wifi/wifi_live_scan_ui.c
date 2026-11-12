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

#include "wifi_live_scan_ui.h"

#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui_manager.h"
#include "ui_chrome.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "msgbox_ui.h"

#define LIVE_REFRESH_MS        200
#define LIVE_VISIBLE_ROWS      6
#define LIVE_ROW_TEXT_SIZE     80
#define LIVE_HEADER_Y          46
#define LIVE_LIST_Y            70
#define LIVE_ROW_HEIGHT        30
#define LIVE_SIDE_MARGIN       8
#define LIVE_ROW_WIDTH_PERCENT 94

static const char *TAG = "WIFI_LIVE_UI";
static lv_obj_t *s_screen;
static lv_obj_t *s_status_label;
static lv_obj_t *s_rows[LIVE_VISIBLE_ROWS];
static lv_timer_t *s_timer;
static wifi_live_scan_ui_done_cb_t s_done_cb;
static void *s_ctx;
static uint32_t s_start_ms;
static uint32_t s_limit_ms;
static bool s_single_sweep;
static uint16_t s_first;
static uint16_t s_visible_rows;
static bool s_is_stopping;

static void refresh(lv_timer_t *timer);
static void on_input(const input_event_t *event, void *ctx);
static void on_delete(lv_event_t *event);

esp_err_t wifi_live_scan_ui_open(uint8_t band, wifi_live_scan_ui_done_cb_t done_cb, void *ctx) {
  if (s_timer != NULL || done_cb == NULL)
    return ESP_ERR_INVALID_STATE;
  esp_err_t err = wifi_live_scan_start(band);
  if (err != ESP_OK)
    return err;
  s_start_ms = lv_tick_get();
  s_limit_ms = 0;
  s_single_sweep = false;
  s_done_cb = done_cb;
  s_ctx = ctx;
  s_first = 0;
  s_visible_rows = (ui_screen_h() - LIVE_LIST_Y - UI_CHROME_FOOTER_H) / LIVE_ROW_HEIGHT;
  if (s_visible_rows > LIVE_VISIBLE_ROWS)
    s_visible_rows = LIVE_VISIBLE_ROWS;
  if (s_visible_rows == 0)
    s_visible_rows = 1;
  s_is_stopping = false;
  lv_obj_t *outgoing = lv_screen_active();
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_screen, "Wi-Fi live scan", "/assets/icons/wifi_find.bin");
  s_status_label = lv_label_create(s_screen);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_LEFT, LIVE_SIDE_MARGIN, LIVE_HEADER_Y);
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  for (uint16_t i = 0; i < s_visible_rows; i++) {
    s_rows[i] = lv_label_create(s_screen);
    lv_obj_set_width(s_rows[i], LV_PCT(LIVE_ROW_WIDTH_PERCENT));
    lv_obj_align(s_rows[i], LV_ALIGN_TOP_LEFT, LIVE_SIDE_MARGIN, LIVE_LIST_Y + i * LIVE_ROW_HEIGHT);
    lv_obj_set_style_text_color(s_rows[i], current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_rows[i], &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_DOT);
    lv_label_set_text(s_rows[i], "");
  }
  ui_chrome_footer(s_screen, "UP/DOWN: browse  BACK: stop");
  lv_obj_add_event_cb(s_screen, on_delete, LV_EVENT_DELETE, NULL);
  s_timer = lv_timer_create(refresh, LIVE_REFRESH_MS, NULL);
  if (s_timer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate refresh timer");
    lv_obj_delete(s_screen);
    return ESP_ERR_NO_MEM;
  }
  ui_screen_load(s_screen);
  ui_input_set_screen_handler(on_input, NULL);
  if (outgoing != NULL && outgoing != s_screen)
    lv_obj_del_async(outgoing);
  ESP_LOGI(TAG, "Live AP list opened");
  return ESP_OK;
}

bool wifi_live_scan_ui_change_page(uint16_t *page, uint16_t page_size, bool forward) {
  if (page == NULL || page_size == 0)
    return false;
  wifi_live_scan_status_t status;
  wifi_live_scan_get_status(&status);
  if (status.is_running)
    return false;
  if (forward && (*page + 1) * page_size < status.count) {
    (*page)++;
    return true;
  }
  if (!forward && *page > 0) {
    (*page)--;
    return true;
  }
  return false;
}

void wifi_live_scan_ui_set_limit(uint32_t duration_ms, bool single_sweep) {
  s_limit_ms = duration_ms;
  s_single_sweep = single_sweep;
}

static void on_delete(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  s_screen = NULL;
  wifi_live_scan_stop();
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  s_done_cb = NULL;
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen) {
    wifi_live_scan_stop();
    lv_timer_delete(s_timer);
    s_timer = NULL;
    s_done_cb = NULL;
    return;
  }
  wifi_live_scan_status_t status;
  wifi_live_scan_get_status(&status);
  if ((s_limit_ms > 0 && lv_tick_elaps(s_start_ms) >= s_limit_ms) ||
      (s_single_sweep && status.sweeps > 0)) {
    s_is_stopping = true;
    wifi_live_scan_stop();
  }
  lv_label_set_text_fmt(s_status_label,
                        "%s: %u APs%s",
                        s_is_stopping ? "Stopping" : "Scanning",
                        status.count,
                        status.is_full ? " (limit)" : "");
  for (uint16_t i = 0; i < s_visible_rows; i++) {
    wifi_ap_record_t record;
    char text[LIVE_ROW_TEXT_SIZE] = "";
    if (wifi_live_scan_get_record(s_first + i, &record) == ESP_OK)
      snprintf(text,
               sizeof(text),
               "%s\nCH %u  %d dBm",
               record.ssid[0] ? (char *)record.ssid : "[hidden]",
               record.primary,
               record.rssi);
    lv_label_set_text(s_rows[i], text);
  }
  if (!status.is_running && s_done_cb != NULL) {
    wifi_live_scan_ui_done_cb_t callback = s_done_cb;
    void *ctx = s_ctx;
    s_done_cb = NULL;
    lv_timer_delete(s_timer);
    s_timer = NULL;
    lv_obj_t *finished = s_screen;
    s_screen = NULL;
    callback(ctx);
    if (status.error != ESP_OK)
      msgbox_open(LV_SYMBOL_WIFI, "Scan interrupted. Check C5 firmware/link.", "OK", NULL, NULL);
    if (lv_screen_active() != finished)
      lv_obj_del_async(finished);
  }
}

static void on_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool is_nav = is_press || event->action == INPUT_ACTION_REPEAT;
  wifi_live_scan_status_t status;
  wifi_live_scan_get_status(&status);
  if (event->button == INPUT_BTN_BACK && is_press) {
    s_is_stopping = true;
    wifi_live_scan_stop();
  } else if (event->button == INPUT_BTN_DOWN && is_nav && s_first + s_visible_rows < status.count) {
    s_first++;
  } else if (event->button == INPUT_BTN_UP && is_nav && s_first > 0) {
    s_first--;
  }
}

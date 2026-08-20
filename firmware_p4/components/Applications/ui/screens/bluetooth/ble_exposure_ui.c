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

#include "ble_exposure_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "exposure_notification.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static const char *TAG = "BLE_EXPOSURE_UI";

#define FEED_TICK_MS 700

#define MAX_ADV      8
#define MAC_LEN      18
#define ROW_LEN      36
#define LIST_BUF_LEN (MAX_ADV * ROW_LEN)

#define EXPO_ICON "/assets/icons/broadcast_on_personal.bin"

#define COUNT_CARD_W 190
#define COUNT_CARD_H 34
#define COUNT_CARD_Y 50
#define LIST_PANEL_W 224
#define LIST_PANEL_H LV_MIN(190, ui_screen_h() - LIST_PANEL_Y - UI_CHROME_FOOTER_H)
#define LIST_PANEL_Y 94
#define LIST_PAD     8

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_feed_timer = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_list_label = NULL;

static void exposure_input(const input_event_t *ev, void *ctx);
static void feed_tick_cb(lv_timer_t *timer);

static void stop_scanning(void) {
  if (s_feed_timer != NULL) {
    lv_timer_delete(s_feed_timer);
    s_feed_timer = NULL;
  }
  exposure_notification_stop();
}

static void render_list(void) {
  uint16_t count = 0;
  exposure_notification_device_t *list = exposure_notification_get_list(&count);

  if (s_list_label != NULL) {
    char buf[LIST_BUF_LEN];
    size_t pos = 0;
    bool first = true;
    for (uint16_t i = 0; i < count && i < MAX_ADV && pos < sizeof(buf) && list != NULL; i++) {
      int m = snprintf(buf + pos,
                       sizeof(buf) - pos,
                       first ? "%02X:%02X:%02X:%02X:%02X:%02X  %d"
                             : "\n%02X:%02X:%02X:%02X:%02X:%02X  %d",
                       list[i].addr[5],
                       list[i].addr[4],
                       list[i].addr[3],
                       list[i].addr[2],
                       list[i].addr[1],
                       list[i].addr[0],
                       list[i].rssi);
      if (m < 0)
        break;
      pos += (size_t)m;
      first = false;
    }
    if (first)
      snprintf(buf, sizeof(buf), "No advertisers");
    lv_label_set_text(s_list_label, buf);
  }

  if (s_count_label != NULL)
    lv_label_set_text_fmt(s_count_label, "Advertisers: %u", (unsigned)count);
}

void ui_ble_exposure_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_feed_timer = NULL;
  s_count_label = NULL;
  s_list_label = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "EXPOSURE", EXPO_ICON);
  ui_chrome_footer(s_screen, "BACK Back");

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_set_size(card, COUNT_CARD_W, COUNT_CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, COUNT_CARD_Y);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);

  s_count_label = lv_label_create(card);
  lv_label_set_text(s_count_label, "Advertisers: 0");
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_center(s_count_label);

  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_set_size(panel, LIST_PANEL_W, LIST_PANEL_H);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, LIST_PANEL_Y);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(panel, 10, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(panel, LIST_PAD, 0);

  s_list_label = lv_label_create(panel);
  lv_obj_set_style_text_color(s_list_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_list_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_list_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(s_list_label, LV_ALIGN_TOP_LEFT, 0, 0);

  exposure_notification_reset();
  if (exposure_notification_start() != ESP_OK) {
    lv_label_set_text(s_list_label, "Radio unavailable");
    notify(NOTIFY_WARNING, "Radio unavailable");
    ESP_LOGE(TAG, "exposure_notification_start failed");
  } else {
    s_feed_timer = lv_timer_create(feed_tick_cb, FEED_TICK_MS, NULL);
  }
  render_list();

  ui_input_set_screen_handler(exposure_input, NULL);

  ui_feedback(UI_FB_SELECT);
  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "BLE exposure screen opened");
}

static void feed_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    stop_scanning();
    return;
  }
  render_list();
}

static void exposure_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        stop_scanning();
        ui_switch_screen(SCREEN_BLE_DETECT_MENU);
      }
      break;
    default:
      break;
  }
}

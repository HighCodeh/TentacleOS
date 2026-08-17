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

#include "ble_flood_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "st7789.h"

#include "ble_connect_flood.h"
#include "ble_l2cap_flood.h"
#include "ble_scanner.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "BLE_FLOOD_UI";

#define SCAN_POLL_MS    200
#define SCAN_TIMEOUT_MS 15000
#define FLOOD_TICK_MS   500

#define FLOOD_ICON "/assets/icons/broadcast_on_personal.bin"

#define BLE_ADDR_LEN 6

#define COL_DIM 0x8A8594

#define CARD_W 172
#define CARD_H 54

#define MODE_CONNECT 0
#define MODE_L2CAP   1

static const char *const FLOOD_MODES[] = {
    "Connect flood",
    "L2CAP flood",
};
#define FLOOD_MODES_COUNT ((int)(sizeof(FLOOD_MODES) / sizeof(FLOOD_MODES[0])))

typedef enum { PHASE_SCAN, PHASE_FLOOD, PHASE_NOTGT } flood_phase_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_target_label = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_rate_label = NULL;
static lv_timer_t *s_flood_timer = NULL;

static flood_phase_t s_phase = PHASE_SCAN;
static uint32_t s_scan_waited = 0;
static uint8_t s_target_addr[BLE_ADDR_LEN];
static uint8_t s_target_type = 0;
static char s_target[40];
static int s_mode = 0;
static int64_t s_flood_start_us = 0;

static void ble_flood_input(const input_event_t *ev, void *ctx);
static void flood_tick_cb(lv_timer_t *timer);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void flood_mode_stop(void) {
  if (s_mode == MODE_L2CAP)
    ble_l2cap_flood_stop();
  else
    ble_connect_flood_stop();
}

static esp_err_t flood_mode_start(void) {
  return (s_mode == MODE_L2CAP) ? ble_l2cap_flood_start(s_target_addr, s_target_type)
                                : ble_connect_flood_start(s_target_addr, s_target_type);
}

static void stop_all(void) {
  if (s_flood_timer != NULL) {
    lv_timer_delete(s_flood_timer);
    s_flood_timer = NULL;
  }
  if (s_phase == PHASE_FLOOD)
    flood_mode_stop();
}

static lv_obj_t *lit_card(lv_obj_t *parent, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 20, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  return card;
}

static void set_mode_text(void) {
  if (s_mode_label != NULL)
    lv_label_set_text_fmt(
        s_mode_label, LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, FLOOD_MODES[s_mode]);
}

void ui_ble_flood_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_mode = 0;
  s_phase = PHASE_SCAN;
  s_scan_waited = 0;
  s_flood_timer = NULL;
  s_target[0] = '\0';

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "BLE FLOOD", FLOOD_ICON);
  ui_chrome_footer(s_screen, "L/R Mode   BACK Stop");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(body, LCD_H_RES, LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);

  s_status_label = lv_label_create(body);
  lv_label_set_text(s_status_label, "Scanning for target...");
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 12);

  s_target_label = lv_label_create(body);
  lv_label_set_text(s_target_label, "");
  lv_obj_set_style_text_color(s_target_label, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(s_target_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_target_label, LV_ALIGN_TOP_MID, 0, 38);

  s_mode_label = lv_label_create(body);
  lv_obj_set_style_text_color(s_mode_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_mode_label, &lv_font_montserrat_14, 0);
  lv_obj_align(s_mode_label, LV_ALIGN_TOP_MID, 0, 72);
  set_mode_text();

  lv_obj_t *card = lit_card(body, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 108);

  s_count_label = lv_label_create(card);
  lv_label_set_text(s_count_label, "--:--");
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_center(s_count_label);

  s_rate_label = lv_label_create(body);
  lv_label_set_text(s_rate_label, "");
  lv_obj_set_style_text_color(s_rate_label, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(s_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_rate_label, LV_ALIGN_TOP_MID, 0, 178);

  fade_in(s_status_label, 200);
  fade_in(s_mode_label, 280);
  fade_in(card, 320);

  if (!ble_scanner_start()) {
    s_phase = PHASE_NOTGT;
    lv_label_set_text(s_status_label, "Radio unavailable");
    ESP_LOGE(TAG, "ble_scanner_start failed");
  } else {
    s_flood_timer = lv_timer_create(flood_tick_cb, SCAN_POLL_MS, NULL);
  }

  ui_input_set_screen_handler(ble_flood_input, NULL);

  ui_feedback(UI_FB_EMULATE);
  ui_screen_load_owned(&s_screen, s_screen);
}

static bool select_target(void) {
  uint16_t n = 0;
  bluetooth_service_scan_result_t *res = ble_scanner_get_results(&n);
  if (res == NULL || n == 0)
    return false;
  int best = 0;
  for (uint16_t i = 1; i < n; i++)
    if (res[i].rssi > res[best].rssi)
      best = i;
  memcpy(s_target_addr, res[best].addr, BLE_ADDR_LEN);
  s_target_type = res[best].addr_type;
  snprintf(s_target,
           sizeof(s_target),
           "%.16s  %02X:%02X:%02X:%02X:%02X:%02X",
           (res[best].name[0] != '\0') ? res[best].name : "(unknown)",
           res[best].addr[5],
           res[best].addr[4],
           res[best].addr[3],
           res[best].addr[2],
           res[best].addr[1],
           res[best].addr[0]);
  return true;
}

static void begin_flood(void) {
  esp_err_t err = flood_mode_start();
  if (err != ESP_OK) {
    lv_label_set_text(s_status_label, "Flood failed");
    ESP_LOGE(TAG, "flood start (mode %d) failed: %s", s_mode, esp_err_to_name(err));
    return;
  }
  s_flood_start_us = esp_timer_get_time();
  lv_label_set_text(s_status_label, "Flooding...");
  notify(NOTIFY_INFO, "Flood started");
}

static void flood_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    stop_all();
    return;
  }

  if (s_phase == PHASE_SCAN) {
    uint16_t dummy = 0;
    s_scan_waited += SCAN_POLL_MS;
    if (ble_scanner_get_results(&dummy) == NULL && s_scan_waited < SCAN_TIMEOUT_MS)
      return;

    bool ok = select_target();
    ble_scanner_free_results();
    if (!ok) {
      s_phase = PHASE_NOTGT;
      lv_label_set_text(s_status_label, "No device to flood");
      return;
    }
    s_phase = PHASE_FLOOD;
    lv_label_set_text(s_target_label, s_target);
    lv_timer_set_period(timer, FLOOD_TICK_MS);
    begin_flood();
    return;
  }

  if (s_phase == PHASE_FLOOD && s_count_label != NULL) {
    int secs = (int)((esp_timer_get_time() - s_flood_start_us) / 1000000);
    lv_label_set_text_fmt(s_count_label, "%02d:%02d", secs / 60, secs % 60);
  }
}

static void ble_flood_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        stop_all();
        ui_switch_screen(SCREEN_BLE_MENU);
      }
      break;
    case INPUT_BTN_RIGHT:
    case INPUT_BTN_LEFT:
      if (nav && s_phase == PHASE_FLOOD) {
        flood_mode_stop();
        s_mode = (ev->button == INPUT_BTN_RIGHT)
                     ? (s_mode + 1) % FLOOD_MODES_COUNT
                     : (s_mode - 1 + FLOOD_MODES_COUNT) % FLOOD_MODES_COUNT;
        set_mode_text();
        fade_in(s_mode_label, 160);
        begin_flood();
        ui_feedback(UI_FB_NAV);
      }
      break;
    default:
      break;
  }
}

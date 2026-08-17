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

#include "ble_track_device_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "ble_scanner.h"
#include "ble_tracker.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "BLE_TRACK_DEV";

#define POLL_MS         300
#define SCAN_POLL_MS    200
#define SCAN_TIMEOUT_MS 15000

#define BLE_ADDR_LEN 6
#define NAME_LEN     24

#define TRACK_ICON "/assets/icons/bluetooth_searching.bin"

#define ARC_SIZE     140
#define ARC_WIDTH    15
#define ARC_ROTATION 270
#define ARC_TOP_Y    50

#define RSSI_MIN   (-95)
#define RSSI_MAX   (-35)
#define RSSI_START (-75)
#define RSSI_CLOSE (-48)

#define COL_COLD 0x2A6FDB
#define COL_WARM 0xFFB300
#define COL_HOT  0x00E676
#define COL_DIM  0x8A8594

typedef enum { PHASE_SCAN, PHASE_TRACK, PHASE_NOTGT } track_phase_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_arc = NULL;
static lv_obj_t *s_dbm_label = NULL;
static lv_obj_t *s_caption = NULL;
static lv_obj_t *s_target_lbl = NULL;
static lv_timer_t *s_timer = NULL;

static track_phase_t s_phase = PHASE_SCAN;
static int s_rssi = RSSI_START;
static int s_prev_rssi = RSSI_START;
static uint32_t s_scan_waited = 0;
static uint8_t s_target_addr[BLE_ADDR_LEN];
static char s_target_name[NAME_LEN];

static void ble_track_device_input(const input_event_t *ev, void *ctx);
static void track_timer_cb(lv_timer_t *t);

static void stop_all(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_phase == PHASE_TRACK)
    ble_tracker_stop();
}

static int rssi_to_pct(int rssi) {
  int pct = (rssi - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN);
  if (pct < 0)
    return 0;
  if (pct > 100)
    return 100;
  return pct;
}

static lv_color_t strength_color(int rssi) {
  if (rssi >= RSSI_CLOSE)
    return lv_color_hex(COL_HOT);
  if (rssi >= RSSI_START)
    return lv_color_hex(COL_WARM);
  return lv_color_hex(COL_COLD);
}

static void apply_reading(int trend) {
  if (s_arc != NULL) {
    lv_arc_set_value(s_arc, rssi_to_pct(s_rssi));
    lv_obj_set_style_arc_color(s_arc, strength_color(s_rssi), LV_PART_INDICATOR);
  }
  if (s_dbm_label != NULL)
    lv_label_set_text_fmt(s_dbm_label, "%d dBm", s_rssi);
  if (s_caption != NULL) {
    if (s_rssi >= RSSI_CLOSE) {
      lv_label_set_text(s_caption, "Very close!");
      lv_obj_set_style_text_color(s_caption, lv_color_hex(COL_HOT), 0);
    } else if (trend > 0) {
      lv_label_set_text(s_caption, LV_SYMBOL_UP "  Warmer");
      lv_obj_set_style_text_color(s_caption, lv_color_hex(COL_WARM), 0);
    } else if (trend < 0) {
      lv_label_set_text(s_caption, LV_SYMBOL_DOWN "  Colder");
      lv_obj_set_style_text_color(s_caption, lv_color_hex(COL_COLD), 0);
    } else {
      lv_label_set_text(s_caption, "Hold steady");
      lv_obj_set_style_text_color(s_caption, lv_color_hex(COL_DIM), 0);
    }
  }
}

void ui_ble_track_device_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
    s_arc = NULL;
  }
  s_rssi = RSSI_START;
  s_prev_rssi = RSSI_START;
  s_phase = PHASE_SCAN;
  s_scan_waited = 0;
  s_timer = NULL;
  s_target_name[0] = '\0';

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "TRACK DEVICE", TRACK_ICON);
  ui_chrome_footer(s_screen, "BACK  Exit");

  s_target_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_target_lbl, "Scanning for target...");
  lv_obj_set_style_text_font(s_target_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_target_lbl, current_theme.text_main, 0);
  lv_obj_align(s_target_lbl, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 2);

  s_arc = lv_arc_create(s_screen);
  lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
  lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, ARC_TOP_Y + 20);
  lv_arc_set_rotation(s_arc, ARC_ROTATION);
  lv_arc_set_bg_angles(s_arc, 0, 360);
  lv_arc_set_range(s_arc, 0, 100);
  lv_arc_set_value(s_arc, rssi_to_pct(s_rssi));
  lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arc, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

  s_dbm_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_dbm_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s_dbm_label, current_theme.text_main, 0);
  lv_obj_align_to(s_dbm_label, s_arc, LV_ALIGN_CENTER, 0, -6);

  s_caption = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_caption, &lv_font_montserrat_14, 0);
  lv_obj_align_to(s_caption, s_arc, LV_ALIGN_CENTER, 0, 16);

  lv_obj_t *tip = lv_label_create(s_screen);
  lv_label_set_text(tip, "Move around to home in");
  lv_obj_set_style_text_font(tip, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tip, lv_color_hex(COL_DIM), 0);
  lv_obj_align(tip, LV_ALIGN_BOTTOM_MID, 0, -(UI_CHROME_FOOTER_H + 14));

  if (s_caption != NULL) {
    lv_label_set_text(s_caption, "Finding devices...");
    lv_obj_set_style_text_color(s_caption, lv_color_hex(COL_DIM), 0);
  }
  lv_obj_fade_in(s_screen, 240, 0);

  ui_input_set_screen_handler(ble_track_device_input, NULL);

  if (!ble_scanner_start()) {
    s_phase = PHASE_NOTGT;
    lv_label_set_text(s_target_lbl, "Radio unavailable");
    ESP_LOGE(TAG, "ble_scanner_start failed");
  } else {
    s_timer = lv_timer_create(track_timer_cb, SCAN_POLL_MS, NULL);
  }

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
  snprintf(s_target_name,
           sizeof(s_target_name),
           "%.*s",
           NAME_LEN - 1,
           (res[best].name[0] != '\0') ? res[best].name : "(unknown)");
  s_rssi = res[best].rssi;
  s_prev_rssi = s_rssi;
  return true;
}

static void track_timer_cb(lv_timer_t *t) {
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
      lv_label_set_text(s_target_lbl, "No device to track");
      if (s_caption != NULL)
        lv_label_set_text(s_caption, "Nothing found");
      return;
    }

    if (ble_tracker_start(s_target_addr) != ESP_OK) {
      s_phase = PHASE_NOTGT;
      lv_label_set_text(s_target_lbl, "Track failed");
      return;
    }
    s_phase = PHASE_TRACK;
    lv_label_set_text_fmt(s_target_lbl, "Target: %s", s_target_name);
    lv_timer_set_period(t, POLL_MS);
    apply_reading(0);
    return;
  }

  if (s_phase == PHASE_TRACK) {
    s_prev_rssi = s_rssi;
    s_rssi = ble_tracker_get_rssi();
    if (s_rssi < RSSI_MIN)
      s_rssi = RSSI_MIN;
    if (s_rssi > RSSI_MAX)
      s_rssi = RSSI_MAX;
    int trend = (s_rssi > s_prev_rssi) ? 1 : (s_rssi < s_prev_rssi) ? -1 : 0;
    apply_reading(trend);
  }
}

static void ble_track_device_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        stop_all();
        ui_switch_screen(SCREEN_BLE_DETECT_MENU);
      }
      break;
    default:
      break;
  }
}

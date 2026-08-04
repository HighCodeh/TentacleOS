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

#include "esp_random.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50
#define DRIFT_MS     420

#define TRACK_ICON  "/assets/icons/bluetooth_searching.bin"
#define TARGET_NAME "JBL-Speaker"

#define ARC_SIZE     140
#define ARC_WIDTH    15
#define ARC_ROTATION 270
#define ARC_TOP_Y    50

#define RSSI_MIN   (-95)
#define RSSI_MAX   (-35)
#define RSSI_START (-75)
#define RSSI_CLOSE (-48)
#define DRIFT_SPAN 7
#define DRIFT_BIAS 3

#define COL_COLD 0x2A6FDB
#define COL_WARM 0xFFB300
#define COL_HOT  0x00E676
#define COL_DIM  0x8A8594

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_arc = NULL;
static lv_obj_t *s_dbm_label = NULL;
static lv_obj_t *s_caption = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_drift_timer = NULL;

static int s_rssi = RSSI_START;

static bool s_back_last = false;
static bool s_left_last = false;

static void nav_timer_cb(lv_timer_t *t);
static void drift_timer_cb(lv_timer_t *t);

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
  s_back_last = false;
  s_left_last = false;
  s_drift_timer = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "TRACK DEVICE", TRACK_ICON);
  ui_chrome_footer(s_screen, "BACK  Exit");

  lv_obj_t *target = lv_label_create(s_screen);
  lv_label_set_text(target, "Target: " TARGET_NAME);
  lv_obj_set_style_text_font(target, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(target, current_theme.text_main, 0);
  lv_obj_align(target, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 2);

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

  apply_reading(0);
  lv_obj_fade_in(s_screen, 240, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  s_drift_timer = lv_timer_create(drift_timer_cb, DRIFT_MS, NULL);

  ui_screen_load(s_screen);
}

static void drift_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    if (s_drift_timer == t)
      s_drift_timer = NULL;
    return;
  }

  int delta = (int)(esp_random() % DRIFT_SPAN) - DRIFT_BIAS;
  int prev = s_rssi;
  s_rssi += delta;
  if (s_rssi < RSSI_MIN)
    s_rssi = RSSI_MIN;
  if (s_rssi > RSSI_MAX)
    s_rssi = RSSI_MAX;

  int trend = 0;
  if (s_rssi > prev)
    trend = 1;
  else if (s_rssi < prev)
    trend = -1;
  apply_reading(trend);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked()) {
    s_back_last = back_button_is_down();
    s_left_last = ui_btn_left();
    return;
  }

  bool back = back_button_is_down();
  bool left = ui_btn_left();
  if ((back && !s_back_last) || (left && !s_left_last)) {
    if (s_drift_timer != NULL) {
      lv_timer_delete(s_drift_timer);
      s_drift_timer = NULL;
    }
    ui_switch_screen(SCREEN_BLE_DETECT_MENU);
  }
  s_back_last = back;
  s_left_last = left;
}

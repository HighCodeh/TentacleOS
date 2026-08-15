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

#include "wifi_signal_locator_ui.h"

#include "esp_log.h"
#include "esp_random.h"

#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "WIFI_SIG_LOC_UI";

#define DRIFT_TICK_MS 320
#define ARC_ANIM_MS   260
#define ENTRY_FADE_MS 240

#define HEADER_ICON "/assets/icons/wifi_find.bin"
#define DBM_FONT    "A:assets/fonts/Inter.bin"
#define TARGET_SSID "HOME-5G"

#define ARC_SIZE     118
#define ARC_WIDTH    13
#define ARC_ROTATION 270
#define RANGE_MIN    0
#define RANGE_MAX    100

#define TARGET_Y  (UI_CHROME_HEADER_H + 6)
#define ARC_TOP_Y (UI_CHROME_HEADER_H + 30)
#define PILL_Y    (ARC_TOP_Y + ARC_SIZE + 10)
#define PROX_Y    (PILL_Y + 32)

#define RSSI_MIN   (-92)
#define RSSI_MAX   (-28)
#define RSSI_START (-70)
#define DRIFT_BIAS 3
#define DRIFT_SPAN 7

#define PCT_FAR 34
#define PCT_MID 67
#define PCT_HOT 90

#define FAR_COLOR  0xE53935
#define MID_COLOR  0xFFB300
#define NEAR_COLOR 0x00E676
#define WARM_COLOR 0x00E676
#define COLD_COLOR 0x29B6F6
#define DIM_COLOR  0x8A8594

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_arc = NULL;
static lv_obj_t *s_dbm_label = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_prox_label = NULL;
static lv_timer_t *s_drift_timer = NULL;
static lv_font_t *s_big_font = NULL;

static int s_rssi = RSSI_START;

static void wifi_signal_locator_input(const input_event_t *ev, void *ctx);
static void drift_tick_cb(lv_timer_t *timer);
static void stop_drift_timer(void);

static int rand_range(int base, int span) {
  return base + (int)(esp_random() % (uint32_t)span);
}

static int rssi_to_pct(int rssi) {
  int pct = (rssi - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN);
  if (pct < RANGE_MIN)
    pct = RANGE_MIN;
  if (pct > RANGE_MAX)
    pct = RANGE_MAX;
  return pct;
}

static lv_color_t heat_color(int pct) {
  if (pct < PCT_FAR)
    return lv_color_hex(FAR_COLOR);
  if (pct < PCT_MID)
    return lv_color_hex(MID_COLOR);
  return lv_color_hex(NEAR_COLOR);
}

static const char *proximity_text(int pct) {
  if (pct < PCT_FAR)
    return "FAR";
  if (pct < PCT_MID)
    return "GETTING CLOSE";
  if (pct < PCT_HOT)
    return "NEAR";
  return "VERY CLOSE";
}

static void arc_anim_exec_cb(void *obj, int32_t v) {
  lv_arc_set_value((lv_obj_t *)obj, v);
}

static void apply_signal(int prev_rssi) {
  int pct = rssi_to_pct(s_rssi);
  lv_color_t c = heat_color(pct);

  if (s_arc != NULL) {
    lv_obj_set_style_arc_color(s_arc, c, LV_PART_INDICATOR);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_arc);
    lv_anim_set_exec_cb(&a, arc_anim_exec_cb);
    lv_anim_set_values(&a, lv_arc_get_value(s_arc), pct);
    lv_anim_set_duration(&a, ARC_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
  }

  if (s_dbm_label != NULL) {
    lv_label_set_text_fmt(s_dbm_label, "%d", s_rssi);
    lv_obj_set_style_text_color(s_dbm_label, c, 0);
  }

  if (s_hint != NULL) {
    lv_color_t hc;
    const char *ht;
    if (s_rssi > prev_rssi) {
      hc = lv_color_hex(WARM_COLOR);
      ht = LV_SYMBOL_UP " WARMER";
    } else if (s_rssi < prev_rssi) {
      hc = lv_color_hex(COLD_COLOR);
      ht = LV_SYMBOL_DOWN " COLDER";
    } else {
      hc = lv_color_hex(DIM_COLOR);
      ht = LV_SYMBOL_MINUS " HOLD";
    }
    lv_label_set_text(s_hint, ht);
    lv_obj_set_style_text_color(s_hint, hc, 0);
    lv_obj_set_style_bg_color(s_hint, hc, 0);
    lv_obj_set_style_border_color(s_hint, hc, 0);
  }

  if (s_prox_label != NULL)
    lv_label_set_text(s_prox_label, proximity_text(pct));
}

void ui_wifi_signal_locator_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
    s_arc = NULL;
    s_dbm_label = NULL;
    s_hint = NULL;
    s_prox_label = NULL;
  }
  stop_drift_timer();

  s_rssi = RSSI_START;

  if (s_big_font == NULL)
    s_big_font = lv_binfont_create(DBM_FONT);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "LOCATOR", HEADER_ICON);
  ui_chrome_footer(s_screen, "BACK: Exit");

  lv_obj_t *target = lv_label_create(s_screen);
  lv_label_set_text(target, LV_SYMBOL_WIFI "  " TARGET_SSID);
  lv_obj_set_style_text_font(target, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(target, current_theme.border_accent, 0);
  lv_obj_set_style_bg_color(target, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(target, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(target, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(target, 1, 0);
  lv_obj_set_style_border_color(target, current_theme.border_accent, 0);
  lv_obj_set_style_pad_hor(target, 14, 0);
  lv_obj_set_style_pad_ver(target, 3, 0);
  lv_obj_align(target, LV_ALIGN_TOP_MID, 0, TARGET_Y);

  s_arc = lv_arc_create(s_screen);
  lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
  lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, ARC_TOP_Y);
  lv_arc_set_rotation(s_arc, ARC_ROTATION);
  lv_arc_set_bg_angles(s_arc, 0, 360);
  lv_arc_set_range(s_arc, RANGE_MIN, RANGE_MAX);
  lv_arc_set_value(s_arc, 0);
  lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arc, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, heat_color(rssi_to_pct(s_rssi)), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

  s_dbm_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_dbm_label, "%d", s_rssi);
  lv_obj_set_style_text_font(s_dbm_label, s_big_font ? s_big_font : &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s_dbm_label, current_theme.text_main, 0);
  lv_obj_align_to(s_dbm_label, s_arc, LV_ALIGN_CENTER, 0, -8);

  lv_obj_t *unit = lv_label_create(s_screen);
  lv_label_set_text(unit, "dBm");
  lv_obj_set_style_text_font(unit, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(unit, lv_color_hex(DIM_COLOR), 0);
  lv_obj_align_to(unit, s_arc, LV_ALIGN_CENTER, 0, 16);

  s_hint = lv_label_create(s_screen);
  lv_label_set_text(s_hint, LV_SYMBOL_MINUS " HOLD");
  lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_opa(s_hint, LV_OPA_20, 0);
  lv_obj_set_style_radius(s_hint, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_hint, 1, 0);
  lv_obj_set_style_pad_hor(s_hint, 12, 0);
  lv_obj_set_style_pad_ver(s_hint, 3, 0);
  lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, PILL_Y);

  s_prox_label = lv_label_create(s_screen);
  lv_label_set_text(s_prox_label, proximity_text(rssi_to_pct(s_rssi)));
  lv_obj_set_style_text_font(s_prox_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_prox_label, lv_color_hex(DIM_COLOR), 0);
  lv_obj_set_style_text_align(s_prox_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_prox_label, LV_ALIGN_TOP_MID, 0, PROX_Y);

  apply_signal(s_rssi);

  lv_obj_fade_in(s_screen, ENTRY_FADE_MS, 0);

  ESP_LOGI(TAG, "signal locator open (mock target %s)", TARGET_SSID);

  ui_input_set_screen_handler(wifi_signal_locator_input, NULL);
  s_drift_timer = lv_timer_create(drift_tick_cb, DRIFT_TICK_MS, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void stop_drift_timer(void) {
  if (s_drift_timer != NULL) {
    lv_timer_delete(s_drift_timer);
    s_drift_timer = NULL;
  }
}

static void drift_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_drift_timer == timer)
      s_drift_timer = NULL;
    return;
  }
  if (s_arc == NULL)
    return;

  int prev = s_rssi;
  s_rssi += rand_range(-DRIFT_BIAS, DRIFT_SPAN);
  if (s_rssi < RSSI_MIN)
    s_rssi = RSSI_MIN;
  if (s_rssi > RSSI_MAX)
    s_rssi = RSSI_MAX;

  apply_signal(prev);
}

static void wifi_signal_locator_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_WIFI_MENU);
      break;
    default:
      break;
  }
}

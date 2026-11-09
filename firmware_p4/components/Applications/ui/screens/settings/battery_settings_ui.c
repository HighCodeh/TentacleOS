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

#include "battery_settings_ui.h"

#include <stdio.h>

#include "esp_log.h"

#include "battery_service.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

static const char *TAG = "BATTERY_SETTINGS_UI";

#define BATTERY_REFRESH_MS   1000
#define BATTERY_FULL_PERCENT 100
#define VOLTAGE_MV_PER_V     1000
#define VOLTAGE_CENTIVOLT_MV 10

#define ARC_SIZE      124
#define ARC_WIDTH     13
#define ARC_ROTATION  270
#define ARC_TOP_Y     46
#define GAUGE_ANIM_MS 700
#define ENTRY_FADE_MS 240
#define STAT_CARD_W   66
#define STAT_CARD_H   46
#define LOW_COLOR     0xE53935
#define MID_COLOR     0xFFB300
#define TITLE_FONT    "A:assets/fonts/Inter.bin"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_arc = NULL;
static lv_font_t *s_big_font = NULL;
static lv_obj_t *s_pct, *s_chg, *s_voltage, *s_source, *s_status, *s_eta;
static lv_timer_t *s_refresh_timer;

static lv_color_t level_color(int pct) {
  if (pct < 25)
    return lv_color_hex(LOW_COLOR);
  if (pct < 60)
    return lv_color_hex(MID_COLOR);
  return lv_color_hex(UI_COL_SUCCESS);
}

static void arc_anim_exec_cb(void *obj, int32_t v) {
  lv_arc_set_value((lv_obj_t *)obj, v);
}

static lv_obj_t *
add_stat_card(lv_obj_t *parent, const char *value, const char *unit, lv_color_t color) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, STAT_CARD_W, STAT_CARD_H);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(card, 11, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(card, 2, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 1, 0);

  lv_obj_t *v = lv_label_create(card);
  lv_label_set_text(v, value);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(v, color, 0);

  lv_obj_t *u = lv_label_create(card);
  lv_label_set_text(u, unit);
  lv_obj_set_style_text_font(u, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(u, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(u, LV_OPA_50, 0);
  return v;
}

static void refresh_cb(lv_timer_t *timer) {
  (void)timer;
  battery_snapshot_t bs = {0};
  bool have = battery_service_get(&bs) && bs.present;
  int soc = have ? bs.soc : 0;
  lv_anim_delete(s_arc, arc_anim_exec_cb);
  lv_arc_set_value(s_arc, soc);
  lv_obj_set_style_arc_color(s_arc, level_color(soc), LV_PART_INDICATOR);
  if (have) {
    lv_label_set_text_fmt(s_pct, "%d%%", soc);
    lv_label_set_text_fmt(s_voltage,
                          "%u.%02u",
                          bs.vbat_mv / VOLTAGE_MV_PER_V,
                          (bs.vbat_mv % VOLTAGE_MV_PER_V) / VOLTAGE_CENTIVOLT_MV);
  } else {
    lv_label_set_text(s_pct, "--");
    lv_label_set_text(s_voltage, "--");
  }
  const char *state = !have             ? "UNAVAILABLE"
                      : bs.charging     ? LV_SYMBOL_CHARGE " CHARGING"
                      : bs.vbus_present ? LV_SYMBOL_CHARGE " ON USB"
                                        : LV_SYMBOL_BATTERY_FULL " ON BATTERY";
  lv_color_t color = have && bs.charging ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_main;
  lv_label_set_text(s_chg, state);
  lv_obj_set_style_text_color(s_chg, color, 0);
  lv_obj_set_style_bg_color(s_chg, color, 0);
  lv_label_set_text(s_source, !have ? "--" : bs.vbus_present ? "USB" : "BATT");
  lv_obj_set_style_text_color(s_source,
                              have && bs.vbus_present ? lv_color_hex(UI_COL_SUCCESS)
                                                      : current_theme.text_main,
                              0);
  lv_label_set_text(s_status,
                    !have         ? "--"
                    : bs.charging ? (bs.chg == CHARGE_STATUS_PRECHARGE ? "PRE" : "FAST")
                    : soc >= BATTERY_FULL_PERCENT ? "FULL"
                                                  : "IDLE");
  lv_obj_set_style_text_color(s_status, color, 0);
  if (have && !bs.vbus_present)
    lv_label_set_text_fmt(s_eta, LV_SYMBOL_BATTERY_FULL " %d%% remaining", soc);
  else
    lv_label_set_text(s_eta,
                      !have                         ? "Battery unavailable"
                      : bs.charging                 ? "Charging"
                      : soc >= BATTERY_FULL_PERCENT ? "Fully charged / on USB"
                                                    : "On USB; not charging");
}

static void screen_delete_cb(lv_event_t *event) {
  (void)event;
  if (s_refresh_timer != NULL) {
    lv_timer_delete(s_refresh_timer);
    s_refresh_timer = NULL;
  }
}

static void battery_settings_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS);
      break;
    default:
      break;
  }
}

void ui_battery_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
    s_arc = NULL;
  }
  (void)TAG;

  if (s_big_font == NULL)
    s_big_font = lv_binfont_create(TITLE_FONT);

  battery_snapshot_t bs = {0};
  bool have = battery_service_get(&bs);
  int soc = have ? bs.soc : 0;
  bool charging = have && bs.charging;
  bool on_usb = have && bs.vbus_present;

  lv_color_t accent = level_color(soc);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "BATTERY", "/assets/icons/battery_full.bin");
  ui_chrome_footer(s_screen, "BACK: Exit");

  s_arc = lv_arc_create(s_screen);
  lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
  lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, ARC_TOP_Y);
  lv_arc_set_rotation(s_arc, ARC_ROTATION);
  lv_arc_set_bg_angles(s_arc, 0, 360);
  lv_arc_set_range(s_arc, 0, 100);
  lv_arc_set_value(s_arc, 0);
  lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arc, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, accent, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

  lv_obj_t *pct = s_pct = lv_label_create(s_screen);
  if (have)
    lv_label_set_text_fmt(pct, "%d%%", soc);
  else
    lv_label_set_text(pct, "--");
  lv_obj_set_style_text_font(pct, s_big_font ? s_big_font : &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(pct, current_theme.text_main, 0);
  lv_obj_align_to(pct, s_arc, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *chg = s_chg = lv_label_create(s_screen);
  uint32_t chip_color;
  if (charging) {
    lv_label_set_text(chg, LV_SYMBOL_CHARGE " CHARGING");
    chip_color = UI_COL_SUCCESS;
  } else if (on_usb) {
    lv_label_set_text(chg, LV_SYMBOL_CHARGE " ON USB");
    chip_color = MID_COLOR;
  } else {
    lv_label_set_text(chg, LV_SYMBOL_BATTERY_FULL " ON BATTERY");
    chip_color = (soc < 25) ? LOW_COLOR : 0x8A8594;
  }
  lv_obj_set_style_text_font(chg, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(chg, lv_color_hex(chip_color), 0);
  lv_obj_set_style_bg_color(chg, lv_color_hex(chip_color), 0);
  lv_obj_set_style_bg_opa(chg, LV_OPA_20, 0);
  lv_obj_set_style_radius(chg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(chg, 9, 0);
  lv_obj_set_style_pad_ver(chg, 2, 0);
  lv_obj_align_to(chg, s_arc, LV_ALIGN_CENTER, 0, 14);

  lv_obj_t *stats = lv_obj_create(s_screen);
  lv_obj_remove_style_all(stats);
  lv_obj_set_size(stats, ui_screen_w() - 24, LV_SIZE_CONTENT);
  lv_obj_align(stats,
               LV_ALIGN_TOP_MID,
               0,
               LV_MIN(ARC_TOP_Y + ARC_SIZE + 16, ui_screen_h() - UI_CHROME_FOOTER_H - STAT_CARD_H));
  lv_obj_remove_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(stats, 8, 0);

  char vbat_s[12];
  snprintf(vbat_s, sizeof(vbat_s), "%u.%02u", bs.vbat_mv / 1000, (bs.vbat_mv % 1000) / 10);
  s_voltage = add_stat_card(stats, have ? vbat_s : "--", "VBAT", current_theme.text_main);
  s_source = add_stat_card(stats,
                           on_usb ? "USB" : "BATT",
                           "SOURCE",
                           on_usb ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_main);

  const char *st = "IDLE";
  lv_color_t st_col = current_theme.border_accent;
  if (charging) {
    st = (bs.chg == CHARGE_STATUS_PRECHARGE) ? "PRE" : "FAST";
    st_col = lv_color_hex(UI_COL_SUCCESS);
  } else if (bs.chg == CHARGE_STATUS_CHARGE_DONE) {
    st = "FULL";
    st_col = lv_color_hex(UI_COL_SUCCESS);
  }
  s_status = add_stat_card(stats, st, "STATUS", st_col);

  lv_obj_t *eta = s_eta = lv_label_create(s_screen);
  if (charging)
    lv_label_set_text(eta, LV_SYMBOL_CHARGE " Charging");
  else if (on_usb)
    lv_label_set_text(eta, LV_SYMBOL_CHARGE " Fully charged / on USB");
  else
    lv_label_set_text_fmt(eta, LV_SYMBOL_BATTERY_FULL " %d%% remaining", soc);
  lv_obj_set_style_text_font(eta, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(eta, current_theme.border_accent, 0);
  lv_obj_set_style_bg_color(eta, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(eta, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(eta, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(eta, 1, 0);
  lv_obj_set_style_border_color(eta, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_hor(eta, 12, 0);
  lv_obj_set_style_pad_ver(eta, 4, 0);
  lv_obj_align_to(eta, stats, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

  lv_obj_fade_in(s_screen, ENTRY_FADE_MS, 0);

  ui_input_set_screen_handler(battery_settings_input, NULL);

  lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);
  refresh_cb(NULL);
  lv_anim_t ga;
  lv_anim_init(&ga);
  lv_anim_set_var(&ga, s_arc);
  lv_anim_set_exec_cb(&ga, arc_anim_exec_cb);
  lv_anim_set_values(&ga, 0, soc);
  lv_anim_set_duration(&ga, GAUGE_ANIM_MS);
  lv_anim_set_path_cb(&ga, lv_anim_path_ease_out);
  lv_anim_start(&ga);

  s_refresh_timer = lv_timer_create(refresh_cb, BATTERY_REFRESH_MS, NULL);
  if (s_refresh_timer == NULL)
    ESP_LOGE(TAG, "Cannot start battery display updates");
  ui_screen_load_owned(&s_screen, s_screen);
}

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

#include "esp_log.h"
#include "battery_service.h"
#include "led_control.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define BATTERY_REFRESH_MS       1000
#define BATTERY_SIDE_PAD         12
#define BATTERY_HERO_HEIGHT      96
#define BATTERY_HERO_WIDE_WIDTH  140
#define BATTERY_GAP              12
#define BATTERY_CARD_RADIUS      12
#define BATTERY_CARD_PAD         12
#define BATTERY_STATUS_Y         10
#define BATTERY_READING_Y        36
#define BATTERY_CAPTION_Y        62
#define BATTERY_METRIC_VALUE_Y   18
#define BATTERY_METRIC_ROW_GAP   50
#define BATTERY_META_GAP         54
#define BATTERY_REASON_GAP       22
#define BATTERY_MV_PER_V         1000
#define BATTERY_MV_PER_CENTIVOLT 10

static const char *TAG = "BATTERY_SETTINGS_UI";
static lv_obj_t *s_screen, *s_pct, *s_source, *s_state, *s_current, *s_reason, *s_note;
static lv_obj_t *s_caption, *s_footer;
static lv_timer_t *s_refresh_timer;

static void refresh_cb(lv_timer_t *timer) {
  (void)timer;
  battery_snapshot_t battery = {.soc = -1};
  battery_charge_snapshot_t charge = {0};
  bool valid = battery_service_get(&battery);
  bool charge_valid = battery_service_get_charge(&charge);
  bool present = valid && battery.present;
  bool known = present && battery.soc >= 0;
  lv_color_t color = current_theme.text_secondary;
  const char *state = "Reading battery";
  if (valid) {
    if (battery.charging) {
      state = "Charging";
      color = lv_color_hex(UI_COL_SUCCESS);
    } else if (battery.chg == CHARGE_STATUS_CHARGE_DONE) {
      state = "Fully charged";
      color = lv_color_hex(UI_COL_SUCCESS);
    } else if (battery.vbus_present) {
      state = charge_valid && !charge.requested ? "Charge paused" : "Not charging";
      color = lv_color_hex(UI_COL_WARNING);
    } else {
      state = battery.low ? "Battery low" : "On battery";
    }
  }
  if (known)
    lv_label_set_text_fmt(s_pct, "%s%d%%", battery.is_soc_estimated ? "~" : "", battery.soc);
  else if (present)
    lv_label_set_text_fmt(s_pct,
                          "%u.%02u V",
                          battery.vbat_mv / BATTERY_MV_PER_V,
                          battery.vbat_mv % BATTERY_MV_PER_V / BATTERY_MV_PER_CENTIVOLT);
  else
    lv_label_set_text(s_pct, "--");
  lv_label_set_text(s_caption,
                    !present                   ? "Battery unavailable"
                    : !known                   ? "Percentage unavailable"
                    : battery.is_soc_estimated ? "Estimated from voltage"
                                               : "State of charge");
  if (!valid)
    lv_label_set_text(s_source, "Unavailable");
  else if (battery.vbus_present && charge_valid)
    lv_label_set_text_fmt(s_source,
                          "USB %u.%02u V",
                          charge.charger.vbus_mv / BATTERY_MV_PER_V,
                          charge.charger.vbus_mv % BATTERY_MV_PER_V / BATTERY_MV_PER_CENTIVOLT);
  else
    lv_label_set_text(s_source, battery.vbus_present ? "USB" : "Battery");
  lv_label_set_text(s_state, state);
  lv_obj_set_style_text_color(s_state, color, 0);
  if (charge_valid && charge.current_valid)
    lv_label_set_text_fmt(s_current, "%+d mA", charge.battery_current_ma);
  else
    lv_label_set_text(s_current, "Unavailable");
  const char *reason = charge_valid && charge.status ? charge.status : "Battery data unavailable";
  if (present && (battery.charging || battery.chg == CHARGE_STATUS_CHARGE_DONE ||
                  !battery.vbus_present || (charge_valid && !charge.requested))) {
    if (known)
      lv_label_set_text_fmt(s_reason,
                            "Battery voltage %u.%02u V",
                            battery.vbat_mv / BATTERY_MV_PER_V,
                            battery.vbat_mv % BATTERY_MV_PER_V / BATTERY_MV_PER_CENTIVOLT);
    else
      lv_label_set_text(s_reason, "Percentage needs calibration");
  } else
    lv_label_set_text(s_reason, reason);
  const battery_service_profile_t *profile = battery_service_profile_get();
  lv_label_set_text_fmt(s_note, "%s  %u mAh", profile->name, profile->capacity_mah);
  ui_chrome_footer_set_text(s_footer,
                            !charge_valid      ? "BACK Exit"
                            : charge.requested ? "OK Pause charge   BACK"
                                               : "OK Resume charge   BACK");
}

void ui_battery_settings_close(void) {
  if (s_refresh_timer != NULL) {
    lv_timer_delete(s_refresh_timer);
    s_refresh_timer = NULL;
  }
}

static void screen_delete_cb(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen)
    ui_battery_settings_close();
}

static void battery_settings_input(const input_event_t *event, void *context) {
  (void)context;
  if (event->action != INPUT_ACTION_PRESS)
    return;
  if (event->button == INPUT_BTN_BACK)
    ui_switch_screen(SCREEN_SETTINGS);
  else if (event->button == INPUT_BTN_OK) {
    battery_charge_snapshot_t charge;
    if (battery_service_get_charge(&charge)) {
      esp_err_t result = battery_service_request_charge(!charge.requested);
      if (result != ESP_OK) {
        ESP_LOGW(TAG, "Charge request failed: %s", esp_err_to_name(result));
        led_signal_warning();
      }
    }
  }
}

static lv_obj_t *add_label(lv_obj_t *parent, const lv_font_t *font, int width) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, width);
  lv_label_set_text(label, "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  return label;
}

void ui_battery_settings_open(void) {
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_screen, "BATTERY", "/assets/icons/battery_full.bin");
  s_footer = ui_chrome_footer(s_screen, "BACK Exit");
  int width = ui_screen_w();
  bool wide = width > ui_screen_h();
  int content_width = width - 2 * BATTERY_SIDE_PAD;
  int top = UI_CHROME_HEADER_H + BATTERY_GAP;
  int hero_width = wide ? BATTERY_HERO_WIDE_WIDTH : content_width;
  lv_obj_t *hero = lv_obj_create(s_screen);
  lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hero, hero_width, BATTERY_HERO_HEIGHT);
  lv_obj_set_pos(hero, BATTERY_SIDE_PAD, top);
  lv_obj_set_style_pad_all(hero, 0, 0);
  lv_obj_set_style_radius(hero, BATTERY_CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(hero, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_color(hero, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(hero, 1, 0);
  int text_width = hero_width - 2 * BATTERY_CARD_PAD;
  s_state = add_label(hero, &lv_font_montserrat_14, text_width);
  lv_obj_set_pos(s_state, BATTERY_CARD_PAD, BATTERY_STATUS_Y);
  s_pct = add_label(hero, &lv_font_montserrat_16, text_width);
  lv_obj_set_pos(s_pct, BATTERY_CARD_PAD, BATTERY_READING_Y);
  s_caption = add_label(hero, &lv_font_montserrat_12, text_width);
  lv_obj_set_style_text_color(s_caption, current_theme.text_secondary, 0);
  lv_obj_set_pos(s_caption, BATTERY_CARD_PAD, BATTERY_CAPTION_Y);

  int metric_x = wide ? BATTERY_SIDE_PAD + hero_width + BATTERY_GAP : BATTERY_SIDE_PAD;
  int metric_y = wide ? top : top + BATTERY_HERO_HEIGHT + BATTERY_GAP;
  int metric_width = wide ? width - metric_x - BATTERY_SIDE_PAD : (content_width - BATTERY_GAP) / 2;
  lv_obj_t *current_title = add_label(s_screen, &lv_font_montserrat_12, metric_width);
  lv_label_set_text(current_title, "NET CURRENT");
  lv_obj_set_style_text_color(current_title, current_theme.text_secondary, 0);
  lv_obj_set_pos(current_title, metric_x, metric_y);
  s_current = add_label(s_screen, &lv_font_montserrat_16, metric_width);
  lv_obj_set_pos(s_current, metric_x, metric_y + BATTERY_METRIC_VALUE_Y);
  int source_x = wide ? metric_x : metric_x + metric_width + BATTERY_GAP;
  int source_y = wide ? metric_y + BATTERY_METRIC_ROW_GAP : metric_y;
  lv_obj_t *source_title = add_label(s_screen, &lv_font_montserrat_12, metric_width);
  lv_label_set_text(source_title, "SUPPLY");
  lv_obj_set_style_text_color(source_title, current_theme.text_secondary, 0);
  lv_obj_set_pos(source_title, source_x, source_y);
  s_source = add_label(s_screen, &lv_font_montserrat_12, metric_width);
  lv_obj_set_pos(s_source, source_x, source_y + BATTERY_METRIC_VALUE_Y);
  int meta_y = wide ? top + BATTERY_HERO_HEIGHT + BATTERY_GAP : metric_y + BATTERY_META_GAP;
  s_note = add_label(s_screen, &lv_font_montserrat_12, content_width);
  lv_obj_set_style_text_color(s_note, current_theme.text_secondary, 0);
  lv_obj_set_pos(s_note, BATTERY_SIDE_PAD, meta_y);
  s_reason = add_label(s_screen, &lv_font_montserrat_12, content_width);
  lv_obj_set_pos(s_reason, BATTERY_SIDE_PAD, meta_y + BATTERY_REASON_GAP);
  lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);
  ui_input_set_screen_handler(battery_settings_input, NULL);
  refresh_cb(NULL);
  s_refresh_timer = lv_timer_create(refresh_cb, BATTERY_REFRESH_MS, NULL);
  if (s_refresh_timer == NULL) {
    ESP_LOGE(TAG, "Battery refresh allocation failed");
    led_signal_error();
    lv_label_set_text(s_reason, "Live updates unavailable");
  }
  ui_screen_load_owned(&s_screen, s_screen);
}

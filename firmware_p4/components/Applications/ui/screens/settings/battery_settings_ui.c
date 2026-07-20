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

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "BATTERY_SETTINGS_UI";

#define NAV_TIMER_MS  50
#define MOCK_PERCENT  76
#define BAR_ANIM_MS   600
#define BOLT_ANIM_MS  900
#define ENTRY_FADE_MS 220

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_bolt = NULL;
static lv_timer_t *s_nav_timer = NULL;

static bool s_back_last = false;

static void add_stat_row(lv_obj_t *parent, const char *label, const char *value) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *l = lv_label_create(row);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, current_theme.text_main, 0);

  lv_obj_t *v = lv_label_create(row);
  lv_label_set_text(v, value);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(v, current_theme.border_accent, 0);
}

static lv_color_t level_color(int pct) {
  if (pct < 25)
    return lv_color_hex(0xE53935);
  if (pct < 60)
    return lv_color_hex(0xFFB300);
  return lv_color_hex(0x00E676);
}

static void bar_anim_exec_cb(void *obj, int32_t v) {
  lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF);
}

static void bolt_opa_exec_cb(void *obj, int32_t v) {
  lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool back = back_button_is_down();
  if (back && !s_back_last)
    ui_switch_screen(SCREEN_SETTINGS);
  s_back_last = back;
}

void ui_battery_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
    s_bar = NULL;
    s_bolt = NULL;
  }
  (void)TAG;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "BATTERY", "/assets/icons/battery_menu_icon.bin");
  ui_chrome_footer(s_screen, "BACK: Exit");

  lv_color_t bar_color = level_color(MOCK_PERCENT);
  s_bar = lv_bar_create(s_screen);
  lv_obj_set_size(s_bar, 190, 16);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 52);
  lv_bar_set_range(s_bar, 0, 100);
  lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_bar, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bar, bar_color, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_bar);
  lv_anim_set_exec_cb(&a, bar_anim_exec_cb);
  lv_anim_set_values(&a, 0, MOCK_PERCENT);
  lv_anim_set_duration(&a, BAR_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  lv_obj_t *pct = lv_label_create(s_screen);
  lv_label_set_text(pct, "76%");
  lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(pct, bar_color, 0);
  lv_obj_align(pct, LV_ALIGN_TOP_MID, 0, 74);

  lv_obj_t *status = lv_label_create(s_screen);
  lv_label_set_text(status, "Charging");
  lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(status, current_theme.border_accent, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 8, 100);

  s_bolt = lv_label_create(s_screen);
  lv_label_set_text(s_bolt, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(s_bolt, current_theme.border_accent, 0);
  lv_obj_align_to(s_bolt, status, LV_ALIGN_OUT_LEFT_MID, -6, 0);

  lv_anim_t b;
  lv_anim_init(&b);
  lv_anim_set_var(&b, s_bolt);
  lv_anim_set_exec_cb(&b, bolt_opa_exec_cb);
  lv_anim_set_values(&b, LV_OPA_30, LV_OPA_COVER);
  lv_anim_set_duration(&b, BOLT_ANIM_MS);
  lv_anim_set_playback_duration(&b, BOLT_ANIM_MS);
  lv_anim_set_repeat_count(&b, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&b, lv_anim_path_ease_in_out);
  lv_anim_start(&b);

  lv_obj_t *stats = lv_obj_create(s_screen);
  lv_obj_remove_style_all(stats);
  lv_obj_set_width(stats, 196);
  lv_obj_set_height(stats, LV_SIZE_CONTENT);
  lv_obj_align(stats, LV_ALIGN_TOP_MID, 0, 124);
  lv_obj_remove_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(stats, 4, 0);

  add_stat_row(stats, "Voltage", "4.02 V");
  add_stat_row(stats, "Current", "+180 mA");
  add_stat_row(stats, "Temp", "31 C");
  add_stat_row(stats, "Cycles", "142");
  add_stat_row(stats, "Health", "96%");
  add_stat_row(stats, "Time to full", "~38 min");

  lv_obj_fade_in(stats, ENTRY_FADE_MS, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

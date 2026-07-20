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

#include "ble_spam_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

#define NAV_TIMER_INTERVAL_MS 50
#define SPAM_TICK_MS          120
#define TARGET_CYCLE_MS       500

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2

static const char *const SPAM_ITEMS[] = {
    "Apple Juice",
    "SourApple",
    "Android",
    "Windows Swift",
    "All",
};
#define SPAM_ITEMS_COUNT (sizeof(SPAM_ITEMS) / sizeof(SPAM_ITEMS[0]))

static const char *const SPAM_TARGETS[] = {
    "iPhone 14",
    "AirPods Pro",
    "Galaxy Buds",
    "MacBook",
    "Mi Band",
};
#define SPAM_TARGETS_COUNT (sizeof(SPAM_TARGETS) / sizeof(SPAM_TARGETS[0]))

static int s_spam_mode = 0;

static lv_obj_t *s_select_screen = NULL;
static menu_component_t s_select_menu;
static lv_timer_t *s_select_nav_timer = NULL;

static bool s_sel_up_last = false;
static bool s_sel_down_last = false;
static bool s_sel_ok_last = false;
static bool s_sel_back_last = false;
static bool s_sel_left_last = false;

static void select_nav_timer_cb(lv_timer_t *timer);

void ui_ble_spam_select_open(void) {
  if (s_select_screen != NULL) {
    lv_obj_del(s_select_screen);
    s_select_screen = NULL;
  }

  s_select_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_select_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_select_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_select_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_select_menu =
      menu_component_create(s_select_screen, "DEVICE SPAM", "/assets/icons/spam_icon.bin");
  for (size_t i = 0; i < SPAM_ITEMS_COUNT; i++)
    menu_component_add_item(&s_select_menu, "/assets/icons/bluetooth_icon.bin", SPAM_ITEMS[i]);

  s_sel_up_last = false;
  s_sel_down_last = false;
  s_sel_ok_last = false;
  s_sel_back_last = false;
  s_sel_left_last = false;

  if (s_select_nav_timer == NULL)
    s_select_nav_timer = lv_timer_create(select_nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load(s_select_screen);
}

static void select_nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_select_screen) {
    lv_timer_delete(timer);
    s_select_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool is_up = ui_btn_up();
  bool is_down = ui_btn_down();
  bool is_ok = ok_button_is_down();
  bool is_back = back_button_is_down();
  bool is_left = left_button_is_down();

  if (is_down && !s_sel_down_last)
    menu_component_next(&s_select_menu);
  if (is_up && !s_sel_up_last)
    menu_component_prev(&s_select_menu);

  if ((is_back && !s_sel_back_last) || (is_left && !s_sel_left_last))
    ui_switch_screen(SCREEN_BLE_MENU);

  if (is_ok && !s_sel_ok_last) {
    int sel = menu_component_get_selected(&s_select_menu);
    if (sel >= 0 && sel < (int)SPAM_ITEMS_COUNT)
      s_spam_mode = sel;
    ui_switch_screen(SCREEN_BLE_SPAM);
  }

  s_sel_up_last = is_up;
  s_sel_down_last = is_down;
  s_sel_ok_last = is_ok;
  s_sel_back_last = is_back;
  s_sel_left_last = is_left;
}

static lv_obj_t *s_run_screen = NULL;
static lv_timer_t *s_run_nav_timer = NULL;
static lv_timer_t *s_run_spam_timer = NULL;
static lv_timer_t *s_run_target_timer = NULL;
static lv_obj_t *s_run_count_label = NULL;
static lv_obj_t *s_run_rate_label = NULL;
static lv_obj_t *s_run_target_label = NULL;
static int s_run_sent = 0;
static int s_run_sent_prev = 0;
static int s_run_tick_accum = 0;
static int s_run_target_idx = 0;

static bool s_run_back_last = false;

static void run_nav_timer_cb(lv_timer_t *timer);
static void run_spam_tick_cb(lv_timer_t *timer);
static void run_target_cycle_cb(lv_timer_t *timer);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void pulse_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void attach_pulse(lv_obj_t *obj, uint32_t period_ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, pulse_opa_cb);
  lv_anim_set_values(&a, LV_OPA_20, LV_OPA_70);
  lv_anim_set_duration(&a, period_ms);
  lv_anim_set_playback_duration(&a, period_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static void run_stop_timers(void) {
  if (s_run_spam_timer != NULL) {
    lv_timer_delete(s_run_spam_timer);
    s_run_spam_timer = NULL;
  }
  if (s_run_target_timer != NULL) {
    lv_timer_delete(s_run_target_timer);
    s_run_target_timer = NULL;
  }
}

void ui_ble_spam_open(void) {
  if (s_run_screen != NULL) {
    lv_obj_del(s_run_screen);
    s_run_screen = NULL;
  }
  s_run_sent = 0;
  s_run_sent_prev = 0;
  s_run_tick_accum = 0;
  s_run_target_idx = 0;
  s_run_spam_timer = NULL;
  s_run_target_timer = NULL;

  s_run_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_run_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_run_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_run_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_run_screen, 0, 0);
  lv_obj_set_style_pad_all(s_run_screen, 0, 0);

  lv_obj_t *header = ui_chrome_header(s_run_screen, "BLE SPAM", "/assets/icons/spam_icon.bin");
  ui_chrome_footer(s_run_screen, "Back  Exit");

  lv_obj_t *status = lv_label_create(s_run_screen);
  lv_label_set_text(status, "Spamming...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 12);

  lv_obj_t *mode = lv_label_create(s_run_screen);
  lv_label_set_text_fmt(mode, "Mode: %s", SPAM_ITEMS[s_spam_mode]);
  lv_obj_set_style_text_color(mode, current_theme.text_main, 0);
  lv_obj_set_style_text_font(mode, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(mode, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(mode, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 36);

  s_run_target_label = lv_label_create(s_run_screen);
  lv_label_set_text_fmt(s_run_target_label, LV_SYMBOL_BLUETOOTH " %s", SPAM_TARGETS[0]);
  lv_obj_set_style_text_color(s_run_target_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_run_target_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_run_target_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_run_target_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 58);

  s_run_count_label = lv_label_create(s_run_screen);
  lv_label_set_text(s_run_count_label, "Sent: 0");
  lv_obj_set_style_text_color(s_run_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_run_count_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(s_run_count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_run_count_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 78);

  s_run_rate_label = lv_label_create(s_run_screen);
  lv_label_set_text(s_run_rate_label, "Adv/s: 0");
  lv_obj_set_style_text_color(s_run_rate_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_run_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_run_rate_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_run_rate_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 102);

  waves_create(s_run_screen, LV_ALIGN_CENTER, 0, 48, LV_SYMBOL_BLUETOOTH, NULL);

  lv_obj_t *bt_glyph = lv_label_create(s_run_screen);
  lv_label_set_text(bt_glyph, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(bt_glyph, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(bt_glyph, &lv_font_montserrat_16, 0);
  lv_obj_align(bt_glyph, LV_ALIGN_CENTER, 64, 48);
  attach_pulse(bt_glyph, 700);

  fade_in(header, 200);
  fade_in(status, 200);
  fade_in(mode, 200);
  fade_in(s_run_count_label, 200);
  fade_in(s_run_rate_label, 200);

  s_run_back_last = false;
  if (s_run_nav_timer == NULL)
    s_run_nav_timer = lv_timer_create(run_nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);
  s_run_spam_timer = lv_timer_create(run_spam_tick_cb, SPAM_TICK_MS, NULL);
  s_run_target_timer = lv_timer_create(run_target_cycle_cb, TARGET_CYCLE_MS, NULL);

  ui_screen_load(s_run_screen);
}

static void run_spam_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_run_screen) {
    lv_timer_delete(timer);
    if (s_run_spam_timer == timer)
      s_run_spam_timer = NULL;
    return;
  }

  s_run_sent++;
  lv_label_set_text_fmt(s_run_count_label, "Sent: %d", s_run_sent);

  s_run_tick_accum++;
  int ticks_per_sec = (1000 + SPAM_TICK_MS - 1) / SPAM_TICK_MS;
  if (s_run_tick_accum >= ticks_per_sec && s_run_rate_label != NULL) {
    int delta = s_run_sent - s_run_sent_prev;
    int per_sec = delta * 1000 / (s_run_tick_accum * SPAM_TICK_MS);
    lv_label_set_text_fmt(s_run_rate_label, "Adv/s: %d", per_sec);
    s_run_sent_prev = s_run_sent;
    s_run_tick_accum = 0;
  }
}

static void run_target_cycle_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_run_screen) {
    lv_timer_delete(timer);
    if (s_run_target_timer == timer)
      s_run_target_timer = NULL;
    return;
  }

  s_run_target_idx = (s_run_target_idx + 1) % (int)SPAM_TARGETS_COUNT;
  lv_label_set_text_fmt(
      s_run_target_label, LV_SYMBOL_BLUETOOTH " %s", SPAM_TARGETS[s_run_target_idx]);
}

static void run_nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_run_screen) {
    lv_timer_delete(timer);
    s_run_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool is_back = back_button_is_down();
  if (is_back && !s_run_back_last) {
    run_stop_timers();
    ui_switch_screen(SCREEN_BLE_SPAM_SELECT);
  }
  s_run_back_last = is_back;
}

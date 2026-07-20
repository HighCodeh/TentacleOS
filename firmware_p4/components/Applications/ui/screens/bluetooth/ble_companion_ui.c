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

#include "ble_companion_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            180
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2
#define NAV_TIMER_INTERVAL_MS  50
#define PAIR_PHASE_MS          3000
#define CHROME_CHILD_COUNT     2

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_phase_timer = NULL;

static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *timer);
static void phase_timer_cb(lv_timer_t *timer);
static void show_success(void);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void pop_size_cb(void *var, int32_t v) {
  lv_obj_set_size((lv_obj_t *)var, v, v);
  lv_obj_center((lv_obj_t *)var);
}
static void pop_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void pop_in(lv_obj_t *obj, int target_px, uint32_t ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
  lv_anim_set_exec_cb(&a, pop_size_cb);
  lv_anim_set_values(&a, 0, target_px);
  lv_anim_start(&a);

  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_set_exec_cb(&a, pop_opa_cb);
  lv_anim_set_values(&a, 0, LV_OPA_COVER);
  lv_anim_start(&a);
}

static lv_obj_t *make_screen_with_title(const char *title) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  lv_obj_t *header = ui_chrome_header(screen, title, "/assets/icons/app_icon.bin");
  fade_in(header, 200);

  ui_chrome_footer(screen, "BACK  Exit");

  return screen;
}

void ui_companion_pairing_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = make_screen_with_title("COMPANION");

  waves_create(s_screen, LV_ALIGN_CENTER, 0, -10, LV_SYMBOL_BLUETOOTH, NULL);

  lv_obj_t *status = lv_label_create(s_screen);
  lv_label_set_text(status, "Waiting for app...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, 92);

  lv_obj_t *code = lv_label_create(s_screen);
  lv_label_set_text(code, "CODE: 4821");
  lv_obj_set_style_text_color(code, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(code, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(code, LV_ALIGN_CENTER, 0, 116);

  fade_in(status, 200);
  fade_in(code, 200);

  s_btn_back_last = false;
  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  s_phase_timer = lv_timer_create(phase_timer_cb, PAIR_PHASE_MS, NULL);
  lv_timer_set_repeat_count(s_phase_timer, 1);

  ui_screen_load(s_screen);
}

static void show_success(void) {
  uint32_t child_count = lv_obj_get_child_count(s_screen);
  for (uint32_t i = child_count; i > CHROME_CHILD_COUNT; i--)
    lv_obj_del(lv_obj_get_child(s_screen, (int32_t)(i - 1)));

  waves_create(s_screen, LV_ALIGN_CENTER, 0, -10, LV_SYMBOL_OK, NULL);

  lv_obj_t *status = lv_label_create(s_screen);
  lv_label_set_text(status, "Companion linked!");
  lv_obj_set_style_text_color(status, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, 78);

  lv_obj_t *linked = lv_label_create(s_screen);
  lv_label_set_text(linked, "Linked: HighBoy-Companion  v1.2");
  lv_obj_set_style_text_color(linked, current_theme.text_main, 0);
  lv_obj_set_style_text_font(linked, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(linked, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(linked, LV_ALIGN_CENTER, 0, 100);

  lv_obj_t *ver = lv_label_create(s_screen);
  lv_label_set_text(ver, "TentacleOS Companion v1.0");
  lv_obj_set_style_text_color(ver, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(ver, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(ver, LV_ALIGN_CENTER, 0, 120);

  lv_obj_t *check_slot = lv_obj_create(s_screen);
  lv_obj_remove_flag(check_slot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(check_slot, 36, 36);
  lv_obj_set_style_bg_opa(check_slot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(check_slot, 0, 0);
  lv_obj_set_style_pad_all(check_slot, 0, 0);
  lv_obj_align(check_slot, LV_ALIGN_CENTER, 0, 50);

  lv_obj_t *check = lv_obj_create(check_slot);
  lv_obj_remove_flag(check, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(check, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(check, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_set_style_bg_opa(check, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(check, 0, 0);
  lv_obj_set_style_pad_all(check, 0, 0);
  lv_obj_set_size(check, 0, 0);
  lv_obj_center(check);

  lv_obj_t *chk_lbl = lv_label_create(check);
  lv_label_set_text(chk_lbl, LV_SYMBOL_OK);
  lv_obj_set_style_text_color(chk_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(chk_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(chk_lbl);

  pop_in(check, 30, 360);
  fade_in(status, 240);
  fade_in(linked, 240);
  fade_in(ver, 240);
}

static void phase_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() == s_screen)
    show_success();
  s_phase_timer = NULL;
  (void)timer;
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked()) {
    s_btn_back_last = back_button_is_down();
    return;
  }

  bool is_back = back_button_is_down();
  if (is_back && !s_btn_back_last) {
    if (s_phase_timer != NULL) {
      lv_timer_delete(s_phase_timer);
      s_phase_timer = NULL;
    }
    ui_switch_screen(SCREEN_BLE_MENU);
  }
  s_btn_back_last = is_back;
}

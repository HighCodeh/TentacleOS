// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ir_controller_ui.h"

#include "esp_log.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_CTRL_UI";

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2
#define NAV_TIMER_INTERVAL_MS  50

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *timer);

void ui_ir_controller_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, OUTER_BORDER, 0);
  lv_obj_set_style_border_color(s_screen, current_theme.border_interface, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *top_area = lv_obj_create(s_screen);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, TOP_AREA_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_BAR_W, TITLE_BAR_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, TITLE_BAR_RADIUS, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, TITLE_BAR_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, "CONTROLLER");
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);

  lv_obj_t *lbl = lv_label_create(s_screen);
  lv_label_set_text(lbl, "Coming soon...");
  lv_obj_set_style_text_color(lbl, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  lv_screen_load(s_screen);
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }

  if (ui_input_is_locked())
    return;

  bool is_back = back_button_is_down();
  if (is_back && !s_btn_back_last)
    ui_switch_screen(SCREEN_IR_MENU);

  s_btn_back_last = is_back;
}
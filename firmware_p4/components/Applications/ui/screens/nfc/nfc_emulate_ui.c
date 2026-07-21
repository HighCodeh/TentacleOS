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

#include "nfc_emulate_ui.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 33
#define CARD_ICON    "/assets/icons/card_icon.bin"
#define FIELD_GREEN  0x00E676

enum { EM_NONE, EM_RUN };

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static bool s_empty = false;

static lv_obj_t *s_ov = NULL;
static nfc_ui_field_t s_ov_field;
static int s_em = EM_NONE;
static uint32_t s_em_start = 0;

static bool s_up_last, s_down_last, s_ok_last, s_back_last, s_left_last, s_right_last;

static void overlay_close(void) {
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
  }
  for (int i = 0; i < 3; i++)
    s_ov_field.ring[i] = NULL;
  s_em = EM_NONE;
}

static void overlay_start(const char *name) {
  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_90, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);

  ui_chrome_header(s_ov, "EMULATE", "/assets/icons/nfc_icon.bin");
  ui_chrome_footer(s_ov, "BACK  Stop");

  lv_obj_t *nm = lv_label_create(s_ov);
  lv_obj_set_width(nm, lv_pct(86));
  lv_label_set_text(nm, name);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(nm, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(nm, LV_ALIGN_TOP_MID, 0, 52);

  nfc_ui_field_create(&s_ov_field, s_ov, lv_color_hex(FIELD_GREEN));

  lv_obj_t *status = lv_label_create(s_ov);
  lv_label_set_text(status, "Present to reader");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -34);

  s_em = EM_RUN;
  s_em_start = lv_tick_get();
  ui_feedback(UI_FB_EMULATE);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  bool up = ui_btn_up(), down = ui_btn_down();
  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (s_em == EM_RUN) {
    nfc_ui_field_tick(&s_ov_field, lv_tick_get() - s_em_start);
    if (back && !s_back_last)
      overlay_close();
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_right_last = right;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  if (ui_input_is_locked()) {
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_right_last = right;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_NFC_MENU);
    return;
  }

  if (!s_empty) {
    if (down && !s_down_last)
      menu_component_next(&s_menu);
    if (up && !s_up_last)
      menu_component_prev(&s_menu);
    if ((ok && !s_ok_last) || (right && !s_right_last)) {
      int sel = menu_component_get_selected(&s_menu);
      const nfc_sim_card_t *c = nfc_sim_saved_get(sel);
      if (c != NULL)
        overlay_start(c->name);
    }
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_nfc_emulate_open(void) {
  nfc_sim_init();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ov = NULL;
  for (int i = 0; i < 3; i++)
    s_ov_field.ring[i] = NULL;
  s_em = EM_NONE;
  s_up_last = s_down_last = s_ok_last = s_back_last = s_left_last = s_right_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  int n = nfc_sim_saved_count();
  s_empty = (n == 0);

  if (s_empty) {
    ui_chrome_header(s_screen, "EMULATE", "/assets/icons/emulate_icon.bin");
    ui_chrome_footer(s_screen, "BACK  Back");
    lv_obj_t *msg = lv_label_create(s_screen);
    lv_label_set_text(msg, "No cards to emulate.\nRead a tag first.");
    lv_obj_set_style_text_color(msg, current_theme.text_main, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(msg);
  } else {
    s_menu = menu_component_create(s_screen, "Emulate", CARD_ICON);
    int cap = n > 10 ? 10 : n;
    for (int i = 0; i < cap; i++)
      menu_component_add_item(&s_menu, CARD_ICON, nfc_sim_saved_get(i)->name);
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

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

#include "nfc_write_ui.h"

#include <stdio.h>

#include "lvgl.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 33
#define ACCENT_GREEN 0x00E676
#define WRITE_ICON   "/assets/icons/write_icon.bin"

enum { WR_NONE, WR_PLACE, WR_WRITING, WR_DONE };
#define T_PLACE   1300
#define T_WRITING 1600
#define T_DONE    900

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static bool s_empty = false;

static lv_obj_t *s_ov = NULL;
static lv_obj_t *s_ov_status = NULL;
static lv_obj_t *s_ov_bar = NULL;
static nfc_ui_field_t s_ov_field;
static int s_wr = WR_NONE;
static uint32_t s_wr_start = 0;

static bool s_up_last, s_down_last, s_ok_last, s_back_last, s_left_last, s_right_last;

static void rings_show(bool show) {
  for (int i = 0; i < 3; i++) {
    if (!s_ov_field.ring[i])
      continue;
    if (show)
      lv_obj_remove_flag(s_ov_field.ring[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_ov_field.ring[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void overlay_close(void) {
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
  }
  s_ov_status = NULL;
  s_ov_bar = NULL;
  for (int i = 0; i < 3; i++)
    s_ov_field.ring[i] = NULL;
  s_wr = WR_NONE;
}

static void overlay_start(const nfc_sim_card_t *card) {
  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_90, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);

  ui_chrome_header(s_ov, "WRITE TAG", "/assets/icons/nfc_card_icon.bin");
  ui_chrome_footer(s_ov, "BACK  Cancel");

  lv_obj_t *panel = nfc_ui_card_panel(s_ov, card);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 46);
  lv_obj_fade_in(panel, 260, 0);

  lv_obj_t *fc = lv_obj_create(s_ov);
  lv_obj_remove_flag(fc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(fc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(fc, lv_pct(100), 130);
  lv_obj_align(fc, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_style_bg_opa(fc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(fc, 0, 0);
  lv_obj_set_style_pad_all(fc, 0, 0);
  nfc_ui_field_create(&s_ov_field, fc, ui_theme_get_accent());

  s_ov_status = lv_label_create(s_ov);
  lv_label_set_text(s_ov_status, "Place blank tag");
  lv_obj_set_style_text_color(s_ov_status, current_theme.text_main, 0);
  lv_obj_align(s_ov_status, LV_ALIGN_BOTTOM_MID, 0, -54);

  s_ov_bar = lv_bar_create(s_ov);
  lv_obj_set_size(s_ov_bar, 180, 12);
  lv_obj_align(s_ov_bar, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_bar_set_range(s_ov_bar, 0, 100);
  lv_bar_set_value(s_ov_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_ov_bar, lv_color_hex(0x202028), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_ov_bar, lv_color_hex(ACCENT_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_ov_bar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(s_ov_bar, 4, LV_PART_INDICATOR);
  lv_obj_add_flag(s_ov_bar, LV_OBJ_FLAG_HIDDEN);

  s_wr = WR_PLACE;
  s_wr_start = lv_tick_get();
}

static void write_tick(void) {
  uint32_t el = lv_tick_get() - s_wr_start;
  if (s_wr == WR_PLACE) {
    nfc_ui_field_tick(&s_ov_field, el);
    int dots = (el / 350) % 4;
    char buf[28];
    snprintf(buf,
             sizeof(buf),
             "Place blank tag%s",
             dots == 1   ? "."
             : dots == 2 ? ".."
             : dots == 3 ? "..."
                         : "");
    lv_label_set_text(s_ov_status, buf);
    if (el >= T_PLACE) {
      rings_show(false);
      lv_obj_remove_flag(s_ov_bar, LV_OBJ_FLAG_HIDDEN);
      s_wr = WR_WRITING;
      s_wr_start = lv_tick_get();
    }
  } else if (s_wr == WR_WRITING) {
    int pct = (int)((uint64_t)el * 100 / T_WRITING);
    if (pct > 100)
      pct = 100;
    lv_label_set_text(s_ov_status, "Writing...");
    lv_bar_set_value(s_ov_bar, pct, LV_ANIM_OFF);
    if (el >= T_WRITING) {
      s_wr = WR_DONE;
      s_wr_start = lv_tick_get();
    }
  } else if (s_wr == WR_DONE) {
    lv_obj_set_style_text_color(s_ov_status, lv_color_hex(ACCENT_GREEN), 0);
    lv_label_set_text(s_ov_status, "Written!");
    lv_bar_set_value(s_ov_bar, 100, LV_ANIM_OFF);
    if (el >= T_DONE)
      overlay_close();
  }
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

  if (s_wr != WR_NONE) {
    write_tick();
    if (s_wr != WR_NONE && back && !s_back_last)
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
        overlay_start(c);
    }
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_nfc_write_open(void) {
  nfc_sim_init();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ov = NULL;
  s_ov_status = NULL;
  s_ov_bar = NULL;
  for (int i = 0; i < 3; i++)
    s_ov_field.ring[i] = NULL;
  s_wr = WR_NONE;
  s_up_last = s_down_last = s_ok_last = s_back_last = s_left_last = s_right_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  int n = nfc_sim_saved_count();
  s_empty = (n == 0);

  if (s_empty) {
    ui_chrome_header(s_screen, "WRITE", WRITE_ICON);
    ui_chrome_footer(s_screen, "BACK  Back");
    lv_obj_t *msg = lv_label_create(s_screen);
    lv_label_set_text(msg, "No cards to write.\nRead a tag first.");
    lv_obj_set_style_text_color(msg, current_theme.text_main, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(msg);
  } else {
    s_menu = menu_component_create(s_screen, "Write", WRITE_ICON);
    int cap = n > 10 ? 10 : n;
    for (int i = 0; i < cap; i++)
      menu_component_add_item(&s_menu, WRITE_ICON, nfc_sim_saved_get(i)->name);
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

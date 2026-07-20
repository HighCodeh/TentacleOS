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

#include "nfc_saved_ui.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50
#define CARD_H       122
#define PEEK         60

enum { DT_NONE, DT_VIEW };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_cont = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_obj_t *s_cards[NFC_SIM_MAX_SAVED];
static int s_count = 0;
static int s_sel = 0;
static bool s_empty = false;

static lv_obj_t *s_ov = NULL;
static int s_detail = DT_NONE;
static int s_sel_idx = -1;

static bool s_up_last, s_down_last, s_ok_last, s_back_last, s_left_last, s_right_last;

static void rebuild_async(void *p) {
  (void)p;
  ui_nfc_saved_open();
}

static void relayout(void) {
  for (int i = 0; i < s_count; i++) {
    int y = i * PEEK;
    if (i > s_sel)
      y += (CARD_H - PEEK);
    lv_obj_align(s_cards[i], LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_border_width(s_cards[i], (i == s_sel) ? 3 : 1, 0);
  }
  int sy = s_sel * PEEK - 6;
  if (sy < 0)
    sy = 0;
  lv_obj_scroll_to_y(s_cont, sy, LV_ANIM_OFF);
}

static void overlay_close(void) {
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
  }
  s_detail = DT_NONE;
}

static void overlay_open(const nfc_sim_card_t *c, int idx) {
  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_80, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);

  lv_obj_t *panel = nfc_ui_card_panel(s_ov, c);
  lv_obj_align(panel, LV_ALIGN_CENTER, 0, -6);

  lv_obj_t *hint = lv_label_create(s_ov);
  lv_label_set_text(hint, LV_SYMBOL_TRASH "  OK = Delete    BACK = Back");
  lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(hint, LV_OPA_70, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_obj_fade_in(s_ov, 220, 0);
  s_detail = DT_VIEW;
  s_sel_idx = idx;
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

  if (s_detail == DT_VIEW) {
    if (back && !s_back_last) {
      overlay_close();
    } else if (ok && !s_ok_last) {
      nfc_sim_remove(s_sel_idx);
      overlay_close();
      lv_async_call(rebuild_async, NULL);
    }
    goto save;
  }

  if (ui_input_is_locked())
    goto save;

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_NFC_MENU);
    return;
  }

  if (!s_empty) {
    if (down && !s_down_last && s_sel < s_count - 1) {
      s_sel++;
      relayout();
    }
    if (up && !s_up_last && s_sel > 0) {
      s_sel--;
      relayout();
    }
    if ((ok && !s_ok_last) || (right && !s_right_last)) {
      const nfc_sim_card_t *c = nfc_sim_saved_get(s_sel);
      if (c != NULL)
        overlay_open(c, s_sel);
    }
  }

save:
  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_nfc_saved_open(void) {
  nfc_sim_init();
  if (s_nav_timer != NULL) {
    lv_timer_delete(s_nav_timer);
    s_nav_timer = NULL;
  }
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ov = NULL;
  s_detail = DT_NONE;
  s_sel_idx = -1;
  s_sel = 0;
  s_up_last = s_down_last = s_ok_last = s_back_last = s_left_last = s_right_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "SAVED", "/assets/icons/card_icon.bin");

  s_count = nfc_sim_saved_count();
  if (s_count > NFC_SIM_MAX_SAVED)
    s_count = NFC_SIM_MAX_SAVED;
  s_empty = (s_count == 0);

  if (s_empty) {
    lv_obj_t *msg = lv_label_create(s_screen);
    lv_label_set_text(msg, "No saved cards.\nRead a tag first.");
    lv_obj_set_style_text_color(msg, current_theme.text_main, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(msg);
  } else {
    int H = lv_display_get_vertical_resolution(NULL);
    if (H < 200)
      H = 320;
    s_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_cont, lv_pct(100), H - 46 - 26);
    lv_obj_align(s_cont, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_opa(s_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cont, 0, 0);
    lv_obj_set_style_pad_all(s_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_cont, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < s_count; i++) {
      s_cards[i] = nfc_ui_card_panel(s_cont, nfc_sim_saved_get(i));
      lv_obj_set_style_shadow_width(s_cards[i], 0, 0);
      lv_obj_set_style_bg_grad_dir(s_cards[i], LV_GRAD_DIR_NONE, 0);
    }
    lv_obj_update_layout(s_screen);
    relayout();

    ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " Browse   OK Open   BACK Exit");
  }

  s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  lv_screen_load(s_screen);
}

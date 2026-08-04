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

#include <stdio.h>

#include "lvgl.h"

#include "st7789.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "capture_result_ui.h"
#include "msgbox_ui.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "notify_ui.h"
#include "page_dots_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS   50
#define CARD_H         122
#define PEEK           60
#define COL_DIM        0x8A8594
#define SAVED_ICON     "/assets/icons/bookmarks.bin"
#define CARD_ICON      "/assets/icons/nfc.bin"
#define CARD_REVEAL_MS 1100
#define UID_VAL_LEN    40

enum { DT_NONE, DT_VIEW };
enum { DV_CARD, DV_CHOICES };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_cont = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_obj_t *s_cards[NFC_SIM_MAX_SAVED];
static page_dots_t s_dots;
static bool s_has_dots = false;
static int s_count = 0;
static int s_sel = 0;
static bool s_empty = false;

static lv_obj_t *s_ov = NULL;
static lv_obj_t *s_ov_card = NULL;
static lv_obj_t *s_ov_footer = NULL;
static capture_result_t s_cr = {0};
static nfc_sim_card_t s_view_card;
static int s_detail = DT_NONE;
static int s_phase = DV_CARD;
static int s_sel_idx = -1;
static uint32_t s_reveal_start = 0;

static bool s_up_last, s_down_last, s_ok_last, s_back_last, s_left_last, s_right_last;

static void rebuild_async(void *p) {
  (void)p;
  ui_nfc_saved_open();
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, 13, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, 16, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  return p;
}

static void build_empty(void) {
  lv_obj_t *card = lit_panel(s_screen, 200, 104);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_image_dsc_t *dsc = assets_get(SAVED_ICON);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  }
  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, "No saved cards");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);
  lv_obj_t *s = lv_label_create(card);
  lv_label_set_text(s, "Read a tag first");
  lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s, lv_color_hex(COL_DIM), 0);
}

static void relayout(void) {
  for (int i = 0; i < s_count; i++) {
    int y = i * PEEK;
    if (i > s_sel)
      y += (CARD_H - PEEK);
    lv_obj_align(s_cards[i], LV_ALIGN_TOP_MID, 0, y);

    bool sel = (i == s_sel);
    if (sel) {
      lv_obj_set_style_border_width(s_cards[i], 3, 0);
      lv_obj_set_style_border_color(s_cards[i], current_theme.border_accent, 0);
      lv_obj_set_style_bg_opa(s_cards[i], LV_OPA_COVER, 0);
      lv_obj_set_style_shadow_color(s_cards[i], current_theme.border_accent, 0);
      lv_obj_set_style_shadow_width(s_cards[i], 14, 0);
      lv_obj_set_style_shadow_opa(s_cards[i], LV_OPA_50, 0);
      lv_obj_set_style_shadow_spread(s_cards[i], -3, 0);
    } else {
      lv_obj_set_style_border_width(s_cards[i], 1, 0);
      lv_obj_set_style_border_color(s_cards[i], nfc_ui_card_color(nfc_sim_saved_get(i)), 0);
      lv_obj_set_style_shadow_width(s_cards[i], 0, 0);
      lv_obj_set_style_shadow_opa(s_cards[i], LV_OPA_TRANSP, 0);
    }
  }
  int sy = s_sel * PEEK - 6;
  if (sy < 0)
    sy = 0;
  lv_obj_scroll_to_y(s_cont, sy, LV_ANIM_OFF);
  if (s_has_dots)
    page_dots_set(&s_dots, s_sel);
}

static void overlay_close(void) {
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
  }
  s_cr = (capture_result_t){0};
  s_ov_card = NULL;
  s_ov_footer = NULL;
  s_detail = DT_NONE;
  s_phase = DV_CARD;
}

static void overlay_open(const nfc_sim_card_t *c, int idx) {
  s_view_card = *c;

  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_90, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);
  lv_obj_set_style_pad_all(s_ov, 0, 0);

  ui_chrome_header(s_ov, "CARD", SAVED_ICON);
  s_ov_footer = ui_chrome_footer(s_ov, "OK  Options    BACK  Back");

  s_ov_card = nfc_ui_card_panel(s_ov, &s_view_card);
  lv_obj_align(s_ov_card, LV_ALIGN_CENTER, 0, -6);
  lv_obj_fade_in(s_ov_card, 220, 0);

  s_detail = DT_VIEW;
  s_phase = DV_CARD;
  s_sel_idx = idx;
  s_reveal_start = lv_tick_get();
  ui_feedback(UI_FB_SELECT);
}

static void choices_step(bool forward) {
  do {
    if (forward)
      capture_result_next(&s_cr);
    else
      capture_result_prev(&s_cr);
  } while (capture_result_selected(&s_cr) != CAP_ACT_PRIMARY &&
           capture_result_selected(&s_cr) != CAP_ACT_DISCARD);
}

static void present_choices(void) {
  if (s_ov_card) {
    lv_obj_del(s_ov_card);
    s_ov_card = NULL;
  }

  char uid[24];
  static char valbuf[UID_VAL_LEN];
  nfc_sim_format_uid(&s_view_card, uid, sizeof(uid));
  snprintf(valbuf, sizeof(valbuf), "UID %s", uid);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = CARD_ICON,
      .card_title = s_view_card.name[0] ? s_view_card.name : s_view_card.type,
      .card_sub = s_view_card.type,
      .card_value = valbuf,
      .primary_label = "Emulate",
  };
  s_cr = capture_result_create(s_ov, &cfg);
  lv_obj_add_flag(s_cr.rows[CAP_ACT_SAVE], LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_cr.rows[CAP_ACT_AGAIN], LV_OBJ_FLAG_HIDDEN);

  ui_chrome_footer_set_text(s_ov_footer, LV_SYMBOL_UP LV_SYMBOL_DOWN " Choose   OK Do   BACK Back");
  s_phase = DV_CHOICES;
}

static void on_del_confirm(bool confirm) {
  if (!confirm)
    return;
  nfc_sim_remove(s_sel_idx);
  ui_feedback(UI_FB_WRITE);
  notify(NOTIFY_INFO, "Card deleted");
  overlay_close();
  lv_async_call(rebuild_async, NULL);
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
    if (!(msgbox_is_open() || ui_input_is_locked())) {
      if (back && !s_back_last) {
        overlay_close();
      } else if (s_phase == DV_CARD) {
        if ((ok && !s_ok_last) || (lv_tick_get() - s_reveal_start >= CARD_REVEAL_MS))
          present_choices();
      } else {
        if (down && !s_down_last) {
          choices_step(true);
          ui_feedback(UI_FB_NAV);
        } else if (up && !s_up_last) {
          choices_step(false);
          ui_feedback(UI_FB_NAV);
        } else if (ok && !s_ok_last) {
          if (capture_result_selected(&s_cr) == CAP_ACT_PRIMARY) {
            ui_feedback(UI_FB_EMULATE);
            ui_switch_screen(SCREEN_NFC_EMULATE);
            return;
          }
          msgbox_open(LV_SYMBOL_TRASH, "Delete this card?", "Delete", "Cancel", on_del_confirm);
        }
      }
    }
    goto save;
  }

  if (ui_input_is_locked() || msgbox_is_open())
    goto save;

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_NFC_MENU);
    return;
  }

  if (!s_empty) {
    if (down && !s_down_last && s_sel < s_count - 1) {
      s_sel++;
      relayout();
      ui_feedback(UI_FB_NAV);
    }
    if (up && !s_up_last && s_sel > 0) {
      s_sel--;
      relayout();
      ui_feedback(UI_FB_NAV);
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
  s_ov_card = NULL;
  s_ov_footer = NULL;
  s_cr = (capture_result_t){0};
  s_has_dots = false;
  s_detail = DT_NONE;
  s_phase = DV_CARD;
  s_sel_idx = -1;
  s_sel = 0;
  s_up_last = s_down_last = s_ok_last = s_back_last = s_left_last = s_right_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "SAVED", SAVED_ICON);

  s_count = nfc_sim_saved_count();
  if (s_count > NFC_SIM_MAX_SAVED)
    s_count = NFC_SIM_MAX_SAVED;
  s_empty = (s_count == 0);

  if (s_empty) {
    build_empty();
    ui_chrome_footer(s_screen, "BACK  Back");
  } else {
    s_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_cont, lv_pct(100), LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
    lv_obj_align(s_cont, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
    lv_obj_set_style_bg_opa(s_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cont, 0, 0);
    lv_obj_set_style_pad_all(s_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_cont, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < s_count; i++) {
      s_cards[i] = nfc_ui_card_panel(s_cont, nfc_sim_saved_get(i));
      lv_obj_set_style_bg_grad_dir(s_cards[i], LV_GRAD_DIR_NONE, 0);
    }
    lv_obj_update_layout(s_screen);
    relayout();

    s_dots = page_dots_create(s_screen, s_count, LV_ALIGN_BOTTOM_MID, 0, -28);
    s_has_dots = true;
    page_dots_set(&s_dots, s_sel);

    ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " Browse   OK Open   BACK Exit");
  }

  s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  lv_screen_load(s_screen);
}

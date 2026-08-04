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

#include "st7789.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "keyboard_ui.h"
#include "msgbox_ui.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "page_dots_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 33
#define SIG_GREEN    0x00E676
#define COL_DIM      0x8A8594
#define WRITE_ICON   "/assets/icons/edit.bin"

#define MAX_CARDS    10
#define CARD_PANEL_H 122
#define SRC_Y        4
#define ARROW_Y      (SRC_Y + CARD_PANEL_H + 4)
#define SLOT_W       150
#define SLOT_H       72
#define SLOT_Y       (ARROW_Y + 26)
#define SLOT_X       ((LCD_H_RES - SLOT_W) / 2)

enum { WR_NONE, WR_PLACE, WR_WRITING, WR_DONE };
#define T_PLACE   1300
#define T_WRITING 1600
#define T_DONE    1000

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_body = NULL;
static page_dots_t s_dots;
static int s_sel = 0;
static int s_count = 0;
static lv_timer_t *s_nav_timer = NULL;
static bool s_empty = false;

static lv_obj_t *s_ov = NULL;
static lv_obj_t *s_ov_status = NULL;
static lv_obj_t *s_ov_footer = NULL;
static lv_obj_t *s_ov_card = NULL;
static lv_obj_t *s_ov_prog = NULL;
static lv_obj_t *s_ov_bar = NULL;
static lv_obj_t *s_ov_ok = NULL;
static nfc_ui_field_t s_ov_field;
static nfc_sim_card_t s_card;
static int s_wr = WR_NONE;
static uint32_t s_wr_start = 0;

static bool s_up_last, s_down_last, s_ok_last, s_back_last, s_left_last, s_right_last;

static void transy_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void card_rise(lv_obj_t *o) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, transy_cb);
  lv_anim_set_values(&a, 26, 0);
  lv_anim_set_duration(&a, 300);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
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

static void build_empty(const char *icon, const char *title, const char *sub) {
  lv_obj_t *card = lit_panel(s_screen, 200, 104);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_image_dsc_t *dsc = assets_get(icon);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  }

  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);

  lv_obj_t *s = lv_label_create(card);
  lv_label_set_text(s, sub);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s, lv_color_hex(COL_DIM), 0);
}

static lv_obj_t *dash_line(lv_obj_t *parent, lv_point_precise_t *pts, int x, int y) {
  lv_obj_t *ln = lv_line_create(parent);
  lv_line_set_points(ln, pts, 2);
  lv_obj_set_pos(ln, x, y);
  lv_obj_set_style_line_color(ln, current_theme.border_accent, 0);
  lv_obj_set_style_line_opa(ln, LV_OPA_80, 0);
  lv_obj_set_style_line_width(ln, 2, 0);
  lv_obj_set_style_line_dash_width(ln, 4, 0);
  lv_obj_set_style_line_dash_gap(ln, 4, 0);
  return ln;
}

static void bench_build(void) {
  static lv_point_precise_t top_pts[2], bot_pts[2], lft_pts[2], rgt_pts[2];

  if (s_body != NULL) {
    lv_obj_del(s_body);
    s_body = NULL;
  }

  s_body = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_body, lv_pct(100), LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_body, 0, 0);
  lv_obj_set_style_pad_all(s_body, 0, 0);

  lv_obj_t *src = nfc_ui_card_panel(s_body, nfc_sim_saved_get(s_sel));
  lv_obj_align(src, LV_ALIGN_TOP_MID, 0, SRC_Y);
  card_rise(src);

  lv_obj_t *arrow = lv_label_create(s_body);
  lv_label_set_text(arrow, LV_SYMBOL_DOWN);
  lv_obj_set_style_text_font(arrow, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(arrow, current_theme.border_accent, 0);
  lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, ARROW_Y);

  top_pts[0].x = 0;
  top_pts[0].y = 0;
  top_pts[1].x = SLOT_W;
  top_pts[1].y = 0;
  bot_pts[0].x = 0;
  bot_pts[0].y = 0;
  bot_pts[1].x = SLOT_W;
  bot_pts[1].y = 0;
  lft_pts[0].x = 0;
  lft_pts[0].y = 0;
  lft_pts[1].x = 0;
  lft_pts[1].y = SLOT_H;
  rgt_pts[0].x = 0;
  rgt_pts[0].y = 0;
  rgt_pts[1].x = 0;
  rgt_pts[1].y = SLOT_H;
  dash_line(s_body, top_pts, SLOT_X, SLOT_Y);
  dash_line(s_body, bot_pts, SLOT_X, SLOT_Y + SLOT_H);
  dash_line(s_body, lft_pts, SLOT_X, SLOT_Y);
  dash_line(s_body, rgt_pts, SLOT_X + SLOT_W, SLOT_Y);

  lv_obj_t *slot_lbl = lv_label_create(s_body);
  lv_label_set_text(slot_lbl, "place blank tag\nto write");
  lv_obj_set_style_text_align(slot_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(slot_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(slot_lbl, current_theme.border_accent, 0);
  lv_obj_align(slot_lbl, LV_ALIGN_TOP_MID, 0, SLOT_Y + 20);

  s_dots = page_dots_create(s_body, s_count, LV_ALIGN_BOTTOM_MID, 0, -2);
  page_dots_set(&s_dots, s_sel);
}

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
  s_ov_footer = NULL;
  s_ov_card = NULL;
  s_ov_prog = NULL;
  s_ov_bar = NULL;
  s_ov_ok = NULL;
  for (int i = 0; i < 3; i++)
    s_ov_field.ring[i] = NULL;
  s_wr = WR_NONE;
}

static void overlay_start(const nfc_sim_card_t *card) {
  s_card = *card;

  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);
  lv_obj_set_style_pad_all(s_ov, 0, 0);

  ui_chrome_header(s_ov, "WRITE TAG", WRITE_ICON);
  s_ov_footer = ui_chrome_footer(s_ov, "BACK  Cancel");

  s_ov_status = lv_label_create(s_ov);
  lv_label_set_text(s_ov_status, "Place blank tag");
  lv_obj_set_style_text_font(s_ov_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_ov_status, current_theme.text_main, 0);
  lv_obj_align(s_ov_status, LV_ALIGN_TOP_MID, 0, 52);

  nfc_ui_field_create(&s_ov_field, s_ov, ui_theme_get_accent());

  s_wr = WR_PLACE;
  s_wr_start = lv_tick_get();
}

static void begin_writing(void) {
  rings_show(false);

  s_ov_card = nfc_ui_card_panel(s_ov, &s_card);
  lv_obj_align(s_ov_card, LV_ALIGN_CENTER, 0, -14);
  lv_obj_fade_in(s_ov_card, 280, 0);
  card_rise(s_ov_card);

  s_ov_prog = lit_panel(s_ov, 200, 28);
  lv_obj_align(s_ov_prog, LV_ALIGN_CENTER, 0, 78);
  lv_obj_set_style_pad_all(s_ov_prog, 0, 0);

  s_ov_bar = lv_bar_create(s_ov_prog);
  lv_obj_set_size(s_ov_bar, 176, 10);
  lv_obj_center(s_ov_bar);
  lv_bar_set_range(s_ov_bar, 0, 100);
  lv_bar_set_value(s_ov_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_ov_bar, lv_color_hex(0x202028), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_ov_bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_ov_bar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(s_ov_bar, 4, LV_PART_INDICATOR);

  lv_label_set_text(s_ov_status, "Writing");
  ui_chrome_footer_set_text(s_ov_footer, "Writing...");
}

static void finish_write(void) {
  lv_obj_set_style_text_color(s_ov_status, lv_color_hex(SIG_GREEN), 0);
  lv_label_set_text(s_ov_status, "Written!");
  if (s_ov_bar) {
    lv_bar_set_value(s_ov_bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ov_bar, lv_color_hex(SIG_GREEN), LV_PART_INDICATOR);
  }
  if (s_ov_card) {
    s_ov_ok = lv_obj_create(s_ov);
    lv_obj_remove_flag(s_ov_ok, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_ov_ok, 30, 30);
    lv_obj_set_style_radius(s_ov_ok, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ov_ok, lv_color_hex(SIG_GREEN), 0);
    lv_obj_set_style_bg_opa(s_ov_ok, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ov_ok, 0, 0);
    lv_obj_align_to(s_ov_ok, s_ov_card, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_t *chk = lv_label_create(s_ov_ok);
    lv_label_set_text(chk, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(chk, lv_color_hex(0x0A0220), 0);
    lv_obj_center(chk);
    lv_obj_fade_in(s_ov_ok, 200, 0);
  }
  ui_chrome_footer_set_text(s_ov_footer, "BACK  Exit");
  ui_feedback(UI_FB_WRITE);
  nfc_ui_play_sound(NFC_SND_SAVE);
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
      begin_writing();
      s_wr = WR_WRITING;
      s_wr_start = lv_tick_get();
    }
  } else if (s_wr == WR_WRITING) {
    int pct = (int)((uint64_t)el * 100 / T_WRITING);
    if (pct > 100)
      pct = 100;
    if (s_ov_bar)
      lv_bar_set_value(s_ov_bar, pct, LV_ANIM_OFF);
    if (el >= T_WRITING) {
      finish_write();
      s_wr = WR_DONE;
      s_wr_start = lv_tick_get();
    }
  } else if (s_wr == WR_DONE) {
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
    if (!(msgbox_is_open() || keyboard_is_open() || ui_input_is_locked())) {
      write_tick();
      if (s_wr != WR_NONE && s_wr != WR_DONE && back && !s_back_last)
        overlay_close();
    }
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_right_last = right;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  if (ui_input_is_locked() || msgbox_is_open() || keyboard_is_open()) {
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
    if (down && !s_down_last) {
      s_sel = (s_sel + 1) % s_count;
      bench_build();
      ui_feedback(UI_FB_NAV);
    }
    if (up && !s_up_last) {
      s_sel = (s_sel == 0) ? s_count - 1 : s_sel - 1;
      bench_build();
      ui_feedback(UI_FB_NAV);
    }
    if ((ok && !s_ok_last) || (right && !s_right_last)) {
      const nfc_sim_card_t *c = nfc_sim_saved_get(s_sel);
      if (c != NULL) {
        ui_feedback(UI_FB_SELECT);
        overlay_start(c);
      }
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
  s_ov_footer = NULL;
  s_ov_card = NULL;
  s_ov_prog = NULL;
  s_ov_bar = NULL;
  s_ov_ok = NULL;
  s_body = NULL;
  s_sel = 0;
  s_count = 0;
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
    build_empty(WRITE_ICON, "Nothing to write", "Read a tag first");
  } else {
    ui_chrome_header(s_screen, "WRITE TAG", WRITE_ICON);
    ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " Source   OK Write   BACK Exit");
    s_count = n > MAX_CARDS ? MAX_CARDS : n;
    s_sel = 0;
    bench_build();
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

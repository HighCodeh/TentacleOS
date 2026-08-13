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

#include "lora_securedm_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50

#define HDR_TITLE   "SECURE DM"
#define HDR_ICON    NULL
#define FOOTER_HINT "UP/DOWN   OK OPEN   BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_BOT (LCD_V_RES - UI_CHROME_FOOTER_H)

#define BANNER_H 36
#define INPUT_H  32

#define BANNER_PAD_H 8
#define BANNER_GAP   6

#define LIST_PAD 8
#define LIST_GAP 6

#define BUBBLE_MAX_W 186
#define BUBBLE_PAD   7
#define BUBBLE_RAD   9

#define LOCK_W    12
#define LOCK_H    14
#define LOCK_SH_W 8
#define LOCK_SH_H 6
#define LOCK_BD_W 12
#define LOCK_BD_H 8
#define LOCK_LINE 2

#define SEAL_SZ 7

#define INPUT_PAD_H 8
#define INPUT_GAP   6
#define PILL_RAD    12
#define PILL_PAD_H  9

#define SCROLL_STEP 36

#define COL_DIM   0x8A8594
#define COL_OK    0x00E676
#define COL_OUTTX 0xF2EEFF
#define COL_OUTGR 0x2A1F52

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_timer_t *s_nav_timer = NULL;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static lv_obj_t *bare_box(lv_obj_t *parent, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

static lv_obj_t *make_lock(lv_obj_t *parent, lv_color_t col) {
  lv_obj_t *box = bare_box(parent, LOCK_W, LOCK_H);

  lv_obj_t *sh = lv_obj_create(box);
  lv_obj_remove_flag(sh, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(sh, LOCK_SH_W, LOCK_SH_H);
  lv_obj_align(sh, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(sh, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(sh, 0, 0);
  lv_obj_set_style_radius(sh, 4, 0);
  lv_obj_set_style_border_width(sh, LOCK_LINE, 0);
  lv_obj_set_style_border_color(sh, col, 0);
  lv_obj_set_style_border_side(
      sh, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);

  lv_obj_t *bd = lv_obj_create(box);
  lv_obj_remove_flag(bd, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bd, LOCK_BD_W, LOCK_BD_H);
  lv_obj_align(bd, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(bd, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(bd, 0, 0);
  lv_obj_set_style_radius(bd, 2, 0);
  lv_obj_set_style_border_width(bd, LOCK_LINE, 0);
  lv_obj_set_style_border_color(bd, col, 0);
  return box;
}

static lv_obj_t *make_seal(lv_obj_t *parent, lv_color_t col) {
  lv_obj_t *s = lv_obj_create(parent);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s, SEAL_SZ, SEAL_SZ);
  lv_obj_set_style_radius(s, 2, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_set_style_bg_color(s, col, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  return s;
}

static void build_banner(lv_obj_t *parent) {
  lv_obj_t *banner = lv_obj_create(parent);
  lv_obj_remove_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(banner, LCD_H_RES, BANNER_H);
  lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_radius(banner, 0, 0);
  lv_obj_set_style_bg_color(banner, lv_color_hex(COL_OK), 0);
  lv_obj_set_style_bg_opa(banner, 30, 0);
  lv_obj_set_style_border_width(banner, 1, 0);
  lv_obj_set_style_border_color(banner, lv_color_hex(COL_OK), 0);
  lv_obj_set_style_border_side(banner, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_opa(banner, LV_OPA_50, 0);
  lv_obj_set_style_pad_hor(banner, BANNER_PAD_H, 0);
  lv_obj_set_style_pad_ver(banner, 0, 0);
  lv_obj_set_style_pad_column(banner, BANNER_GAP, 0);
  lv_obj_set_flex_flow(banner, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(banner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  make_lock(banner, lv_color_hex(COL_OK));

  lv_obj_t *col = bare_box(banner, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 1, 0);
  lv_obj_set_flex_grow(col, 1);

  lv_obj_t *nm = lv_label_create(col);
  lv_label_set_text(nm, "Ghost");
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);

  lv_obj_t *fp = lv_label_create(col);
  lv_label_set_text(fp,
                    "verified \xE2\x80\xA2 3A9F\xE2\x80\xA2"
                    "C21E");
  lv_obj_set_style_text_font(fp, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(fp, lv_color_hex(COL_OK), 0);

  lv_obj_t *e2ee = lv_label_create(banner);
  lv_label_set_text(e2ee, "E2EE");
  lv_obj_set_style_text_font(e2ee, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(e2ee, lv_color_hex(COL_OK), 0);
}

static void add_bubble(lv_obj_t *list, bool outgoing, const char *text, const char *ts) {
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row,
                        outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *bubble = lv_obj_create(row);
  lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(bubble, BUBBLE_MAX_W, 0);
  lv_obj_set_style_pad_all(bubble, BUBBLE_PAD, 0);
  lv_obj_set_style_radius(bubble, BUBBLE_RAD, 0);
  lv_obj_set_style_pad_row(bubble, 3, 0);
  lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_border_width(bubble, 1, 0);
  if (outgoing) {
    lv_obj_set_style_bg_color(bubble, current_theme.border_accent, 0);
    lv_obj_set_style_bg_grad_color(bubble, lv_color_hex(COL_OUTGR), 0);
    lv_obj_set_style_bg_grad_dir(bubble, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bubble, current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_bg_color(bubble, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_dir(bubble, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bubble, current_theme.border_inactive, 0);
  }

  lv_obj_t *body = lv_label_create(bubble);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(body, BUBBLE_MAX_W - 2 * BUBBLE_PAD, 0);
  lv_label_set_text(body, text);
  lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      body, outgoing ? lv_color_hex(COL_OUTTX) : current_theme.text_main, 0);

  lv_obj_t *meta = bare_box(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(meta, 4, 0);
  if (outgoing) {
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  } else {
    make_seal(meta, lv_color_hex(COL_OK));
  }

  lv_obj_t *tlbl = lv_label_create(meta);
  if (outgoing) {
    char buf[24];
    lv_snprintf(buf, sizeof(buf), "%s %s%s", ts, LV_SYMBOL_OK, LV_SYMBOL_OK);
    lv_label_set_text(tlbl, buf);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(COL_OUTTX), 0);
    lv_obj_set_style_text_opa(tlbl, LV_OPA_70, 0);
  } else {
    lv_label_set_text(tlbl, ts);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(COL_DIM), 0);
  }
  lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_12, 0);
}

static void build_list(lv_obj_t *parent) {
  s_list = lv_obj_create(parent);
  lv_obj_set_size(s_list, LCD_H_RES, BODY_BOT - INPUT_H - (BODY_TOP + BANNER_H));
  lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, BODY_TOP + BANNER_H);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_list, 0, 0);
  lv_obj_set_style_pad_all(s_list, LIST_PAD, 0);
  lv_obj_set_style_pad_row(s_list, LIST_GAP, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(s_list, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(s_list, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(s_list, 2, LV_PART_SCROLLBAR);

  add_bubble(s_list, false, "coords received, moving to grid RP2", "09:41");
  add_bubble(s_list, true, "copy. hold at RP2, report pax", "09:42");
  add_bubble(s_list, false, "eyes on gate - 2 pax, 1 vehicle", "09:44");
}

static void build_input(lv_obj_t *parent) {
  lv_obj_t *strip = lv_obj_create(parent);
  lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(strip, LCD_H_RES, INPUT_H);
  lv_obj_align(strip, LV_ALIGN_TOP_MID, 0, BODY_BOT - INPUT_H);
  lv_obj_set_style_radius(strip, 0, 0);
  lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(strip, 1, 0);
  lv_obj_set_style_border_color(strip, current_theme.border_inactive, 0);
  lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_hor(strip, INPUT_PAD_H, 0);
  lv_obj_set_style_pad_ver(strip, 0, 0);
  lv_obj_set_style_pad_column(strip, INPUT_GAP, 0);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  make_lock(strip, lv_color_hex(COL_OK));

  lv_obj_t *pill = lv_obj_create(strip);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(pill, 22);
  lv_obj_set_flex_grow(pill, 1);
  lv_obj_set_style_radius(pill, PILL_RAD, 0);
  lv_obj_set_style_bg_color(pill, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pill, 1, 0);
  lv_obj_set_style_border_color(pill, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_hor(pill, PILL_PAD_H, 0);
  lv_obj_set_style_pad_ver(pill, 0, 0);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ph = lv_label_create(pill);
  lv_label_set_text(ph, "sealed message...");
  lv_obj_set_style_text_font(ph, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(ph, lv_color_hex(COL_DIM), 0);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_LORA_CHAT);
    return;
  }
  if (down && !s_down_last && s_list) {
    int32_t sb = lv_obj_get_scroll_bottom(s_list);
    if (sb > 0)
      lv_obj_scroll_by(s_list, 0, -(sb < SCROLL_STEP ? sb : SCROLL_STEP), LV_ANIM_ON);
  }
  if (up && !s_up_last && s_list) {
    int32_t st = lv_obj_get_scroll_top(s_list);
    if (st > 0)
      lv_obj_scroll_by(s_list, 0, (st < SCROLL_STEP ? st : SCROLL_STEP), LV_ANIM_ON);
  }
  if (ok && !s_ok_last)
    ui_feedback(UI_FB_SELECT);

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_lora_securedm_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_list = NULL;
  s_up_last = s_down_last = s_left_last = s_ok_last = s_back_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_banner(s_screen);
  build_list(s_screen);
  build_input(s_screen);

  ui_chrome_footer(s_screen, FOOTER_HINT);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

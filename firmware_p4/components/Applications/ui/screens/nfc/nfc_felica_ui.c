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

#include "nfc_felica_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50

#define MX        8
#define BODY_H    (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define CONTENT_W (LCD_H_RES - 2 * MX)
#define ROW_GAP   6

#define SVC_COUNT 3
#define ROW_H     30
#define ROW_RAD   8

#define CHIP_H   16
#define CHIP_RAD 8
#define CHIP_PAD 6

#define DUMP_RAD 8
#define DUMP_PAD 8

#define COL_DIM    0x8A8594
#define COL_CYAN   0x37E0A8
#define COL_LINE   0x2A2636
#define COL_PANEL2 0x1A1626
#define DIM_HEX    "8A8594"

#define HDR_TITLE   "FELICA"
#define HDR_ICON    "/assets/icons/nfc.bin"
#define FOOTER_HINT "OK read   UP/DOWN service   BACK"

#define TXT_SYS  "Sys 0003"
#define TXT_NAME "Suica"

#define BLK0 "#" DIM_HEX " blk0 # 16 00 07 21 0A 3E ..."
#define BLK1 "#" DIM_HEX " blk1 # JPY 2,480 - gate 0A21"

typedef struct {
  const char *code;
  const char *desc;
} svc_def_t;

static const svc_def_t SVCS[SVC_COUNT] = {
    {"Svc 090F", "history - ro"},
    {"Svc 1A8B", "balance - ro"},
    {"Svc 004B", "id - ro"},
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_row[SVC_COUNT];
static lv_obj_t *s_code[SVC_COUNT];
static lv_timer_t *s_nav_timer = NULL;

static int s_sel = 0;
static bool s_ok_last = false;
static bool s_back_last = false;
static bool s_left_last = false;
static bool s_up_last = false;
static bool s_down_last = false;

static void make_chip(lv_obj_t *parent, const char *txt, bool sel) {
  lv_obj_t *chip = lv_obj_create(parent);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(chip, CHIP_H);
  lv_obj_set_width(chip, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(chip, CHIP_RAD, 0);
  lv_obj_set_style_pad_hor(chip, CHIP_PAD, 0);
  lv_obj_set_style_pad_ver(chip, 0, 0);
  lv_obj_set_style_bg_color(chip, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(chip, sel ? LV_OPA_30 : LV_OPA_10, 0);
  lv_obj_set_style_border_color(chip, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(chip, sel ? 1 : 0, 0);

  lv_obj_t *l = lv_label_create(chip);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, sel ? current_theme.border_accent : lv_color_hex(COL_DIM), 0);
  lv_obj_center(l);
}

static lv_obj_t *make_row(lv_obj_t *parent, int i) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, ROW_H);
  lv_obj_set_style_radius(row, ROW_RAD, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_left(row, 8, 0);
  lv_obj_set_style_pad_right(row, 8, 0);
  lv_obj_set_style_pad_ver(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *code = lv_label_create(row);
  lv_label_set_text(code, SVCS[i].code);
  lv_obj_set_style_text_font(code, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(code, current_theme.text_main, 0);
  lv_obj_set_flex_grow(code, 1);

  lv_obj_t *desc = lv_label_create(row);
  lv_label_set_text(desc, SVCS[i].desc);
  lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(desc, lv_color_hex(COL_DIM), 0);

  s_code[i] = code;
  return row;
}

static void refresh_selection(void) {
  for (int i = 0; i < SVC_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(
        s_row[i], sel ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(s_row[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        s_row[i], sel ? current_theme.bg_primary : current_theme.bg_secondary, 0);
    lv_obj_set_style_shadow_width(s_row[i], sel ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(s_row[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_spread(s_row[i], sel ? -3 : 0, 0);
    lv_obj_set_style_text_color(
        s_code[i], sel ? current_theme.border_accent : current_theme.text_main, 0);
  }
}

static void build_dump(lv_obj_t *parent) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(box, CONTENT_W);
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(box, DUMP_RAD, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(COL_PANEL2), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(COL_LINE), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_pad_all(box, DUMP_PAD, 0);
  lv_obj_set_style_pad_row(box, 4, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

  const char *lines[] = {BLK0, BLK1};
  for (int i = 0; i < 2; i++) {
    lv_obj_t *ln = lv_label_create(box);
    lv_label_set_recolor(ln, true);
    lv_label_set_text(ln, lines[i]);
    lv_obj_set_style_text_font(ln, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ln, lv_color_hex(COL_CYAN), 0);
  }
}

static void build_body(lv_obj_t *parent) {
  lv_obj_t *chips = lv_obj_create(parent);
  lv_obj_remove_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(chips, lv_pct(100));
  lv_obj_set_height(chips, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(chips, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chips, 0, 0);
  lv_obj_set_style_pad_all(chips, 0, 0);
  lv_obj_set_style_pad_column(chips, 5, 0);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_chip(chips, TXT_SYS, true);
  make_chip(chips, TXT_NAME, false);

  for (int i = 0; i < SVC_COUNT; i++)
    s_row[i] = make_row(parent, i);
  refresh_selection();

  build_dump(parent);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool ok = ok_button_is_down();
  bool back = back_button_is_down();
  bool left = ui_btn_left();
  bool up = ui_btn_up();
  bool down = ui_btn_down();

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_NFC_MENU);
    return;
  }
  if (down && !s_down_last) {
    s_sel = (s_sel + 1) % SVC_COUNT;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (up && !s_up_last) {
    s_sel = (s_sel - 1 + SVC_COUNT) % SVC_COUNT;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (ok && !s_ok_last)
    ui_feedback(UI_FB_SELECT);

  s_ok_last = ok;
  s_back_last = back;
  s_left_last = left;
  s_up_last = up;
  s_down_last = down;
}

void ui_nfc_felica_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;
  s_ok_last = s_back_last = s_left_last = s_up_last = s_down_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(body, LCD_H_RES, BODY_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, MX, 0);
  lv_obj_set_style_pad_row(body, ROW_GAP, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  build_body(body);

  ui_chrome_footer(s_screen, FOOTER_HINT);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

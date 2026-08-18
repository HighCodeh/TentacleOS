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

#include "nfc_iso15693_ui.h"

#include <stdio.h>

#include "lvgl.h"
#include "st7789.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define MX        8
#define BODY_H    (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define CONTENT_W (LCD_H_RES - 2 * MX)
#define ROW_GAP   7

#define BLOCK_COUNT 28
#define GRID_COLS   7
#define LOCKED_MAX  4
#define SEL_START   5

#define CELL_W   28
#define CELL_H   24
#define CELL_GAP 3
#define CELL_RAD 3
#define GRID_W   (GRID_COLS * CELL_W + (GRID_COLS - 1) * CELL_GAP)

#define CHIP_H   16
#define CHIP_RAD 8
#define CHIP_PAD 6

#define COL_GOLD   0xD9A521
#define COL_LINE   0x2A2636
#define COL_PANEL2 0x1A1626

#define HDR_TITLE   "NFC-V / 15693"
#define HDR_ICON    "/assets/icons/nfc.bin"
#define FOOTER_HINT "OK read/write   UP/DOWN block   BACK"

#define TXT_TAG   "ICODE SLIX"
#define TXT_GEOM  "28x4 B"
#define TXT_READ  "READ"
#define TXT_WRITE "WRITE"
#define TXT_LOCK  "4 locked"
#define KV_BLOCK  "Block"

#define IDX_BUF 12
#define VAL_BUF 32

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_cell[BLOCK_COUNT];
static lv_obj_t *s_cell_lbl[BLOCK_COUNT];
static lv_obj_t *s_block_key = NULL;
static lv_obj_t *s_block_val = NULL;

static int s_sel = SEL_START;

static void format_block_val(int i, char *buf, size_t n) {
  snprintf(buf, n, "%02X FF 00 %02X", (unsigned)i, (unsigned)((i * 7 + 3) & 0xFF));
}

static lv_obj_t *make_chip(lv_obj_t *parent, const char *txt, bool sel) {
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
  lv_obj_set_style_text_color(l, sel ? current_theme.border_accent : current_theme.text_secondary, 0);
  lv_obj_center(l);
  return chip;
}

static void restyle_cells(void) {
  for (int i = 0; i < BLOCK_COUNT; i++) {
    bool sel = (i == s_sel);
    bool locked = (i < LOCKED_MAX);
    lv_color_t border = sel      ? current_theme.border_accent
                        : locked ? lv_color_hex(COL_GOLD)
                                 : lv_color_hex(COL_LINE);
    lv_color_t txt = sel      ? current_theme.text_main
                     : locked ? lv_color_hex(COL_GOLD)
                              : current_theme.text_secondary;
    lv_obj_set_style_border_color(s_cell[i], border, 0);
    lv_obj_set_style_bg_color(
        s_cell[i], sel ? current_theme.bg_primary : lv_color_hex(COL_PANEL2), 0);
    lv_obj_set_style_shadow_width(s_cell[i], sel ? 7 : 0, 0);
    lv_obj_set_style_shadow_color(s_cell[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_opa(s_cell[i], sel ? LV_OPA_50 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_cell_lbl[i], txt, 0);
  }
}

static void update_block_kv(void) {
  static char kbuf[IDX_BUF + 8];
  static char vbuf[VAL_BUF];
  snprintf(kbuf, sizeof(kbuf), "%s %02X", KV_BLOCK, (unsigned)s_sel);
  format_block_val(s_sel, vbuf, sizeof(vbuf));
  lv_label_set_text(s_block_key, kbuf);
  lv_label_set_text(s_block_val, vbuf);
}

static void build_grid(lv_obj_t *parent) {
  lv_obj_t *grid = lv_obj_create(parent);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(grid, GRID_W);
  lv_obj_set_height(grid, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_row(grid, CELL_GAP, 0);
  lv_obj_set_style_pad_column(grid, CELL_GAP, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

  for (int i = 0; i < BLOCK_COUNT; i++) {
    lv_obj_t *cell = lv_obj_create(grid);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(cell, CELL_W, CELL_H);
    lv_obj_set_style_radius(cell, CELL_RAD, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);

    lv_obj_t *l = lv_label_create(cell);
    char b[IDX_BUF];
    snprintf(b, sizeof(b), "%02X", (unsigned)i);
    lv_label_set_text(l, b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);

    s_cell[i] = cell;
    s_cell_lbl[i] = l;
  }
  restyle_cells();
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
  make_chip(chips, TXT_TAG, true);
  make_chip(chips, TXT_GEOM, false);

  build_grid(parent);

  lv_obj_t *kv = lv_obj_create(parent);
  lv_obj_remove_flag(kv, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(kv, lv_pct(100));
  lv_obj_set_height(kv, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(kv, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(kv, 0, 0);
  lv_obj_set_style_pad_all(kv, 0, 0);
  lv_obj_set_flex_flow(kv, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      kv, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  s_block_key = lv_label_create(kv);
  lv_obj_set_style_text_font(s_block_key, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_block_key, current_theme.text_secondary, 0);
  s_block_val = lv_label_create(kv);
  lv_obj_set_style_text_font(s_block_val, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_block_val, current_theme.text_main, 0);
  update_block_kv();

  lv_obj_t *actions = lv_obj_create(parent);
  lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(actions, lv_pct(100));
  lv_obj_set_height(actions, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_set_style_pad_all(actions, 0, 0);
  lv_obj_set_style_pad_column(actions, 6, 0);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_chip(actions, TXT_READ, true);
  make_chip(actions, TXT_WRITE, false);
  lv_obj_t *lock = lv_label_create(actions);
  lv_label_set_text(lock, TXT_LOCK);
  lv_obj_set_style_text_font(lock, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lock, lv_color_hex(COL_GOLD), 0);
}

static void nfc_iso15693_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % BLOCK_COUNT;
        restyle_cells();
        update_block_kv();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel - 1 + BLOCK_COUNT) % BLOCK_COUNT;
        restyle_cells();
        update_block_kv();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press)
        ui_feedback(UI_FB_SELECT);
      break;
    default:
      break;
  }
}

void ui_nfc_iso15693_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = SEL_START;

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
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  build_body(body);

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(nfc_iso15693_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

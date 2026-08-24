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

#include "nfc_desfire_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define MX        8
#define BODY_H    (ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define CONTENT_W (ui_screen_w() - 2 * MX)
#define ROW_GAP   6

#define CARD_H   74
#define CARD_PAD 8
#define CARD_RAD 8

#define CHIP_W   58
#define CHIP_H   16
#define CHIP_RAD 8

#define DUMP_RAD 8
#define DUMP_PAD 8

#define COL_CYAN   0x37E0A8
#define COL_PANEL2 0x1A1626
#define COL_LINE   0x2A2636

#define ACC_HEX "B89AFF"
#define DIM_HEX "8A8594"

#define HDR_TITLE   "DESFIRE"
#define HDR_ICON    "/assets/icons/nfc.bin"
#define FOOTER_HINT "OK open   UP/DOWN nav   BACK"

#define TXT_AUTH "AUTH OK"
#define TXT_ALG  "AES-128"
#define KV_KEY_K "Key"
#define KV_KEY_V "#00 master"
#define KV_SES_K "Session"
#define KV_SES_V "CMAC"
#define TXT_FILE "FILE F1 - 48 B"

#define DUMP_L0 "04 D3 A1 22 55 6A 90 00"
#define DUMP_L1 "1A FF 00 12 08 27 03 E8"
#define DUMP_L2 "00 00 00 64 00 00 27 10"
#define DUMP_L3 "#" DIM_HEX " ... +24 B#"

static const char *const DUMP_LINES[] = {DUMP_L0, DUMP_L1, DUMP_L2, DUMP_L3};
#define DUMP_LINE_COUNT ((int)(sizeof(DUMP_LINES) / sizeof(DUMP_LINES[0])))

static lv_obj_t *s_screen = NULL;

static void make_kv(lv_obj_t *parent, const char *k, const char *v) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *kl = lv_label_create(row);
  lv_label_set_text(kl, k);
  lv_obj_set_style_text_font(kl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(kl, current_theme.text_secondary, 0);

  lv_obj_t *vl = lv_label_create(row);
  lv_label_set_text(vl, v);
  lv_obj_set_style_text_font(vl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(vl, current_theme.text_main, 0);
}

static void build_auth_card(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, CARD_H);
  lv_obj_set_style_radius(card, CARD_RAD, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, CARD_PAD, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *top = lv_obj_create(card);
  lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(top, lv_pct(100));
  lv_obj_set_height(top, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_style_pad_all(top, 0, 0);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, 6, 0);

  lv_obj_t *ok = lv_label_create(top);
  lv_label_set_text(ok, LV_SYMBOL_OK " " TXT_AUTH);
  lv_obj_set_style_text_font(ok, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(ok, lv_color_hex(UI_COL_SUCCESS), 0);

  lv_obj_t *spacer = lv_obj_create(top);
  lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(spacer, 1);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(spacer, 0, 0);
  lv_obj_set_style_pad_all(spacer, 0, 0);
  lv_obj_set_flex_grow(spacer, 1);

  lv_obj_t *chip = lv_obj_create(top);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, CHIP_W, CHIP_H);
  lv_obj_set_style_radius(chip, CHIP_RAD, 0);
  lv_obj_set_style_bg_color(chip, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
  lv_obj_set_style_border_color(chip, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_pad_all(chip, 0, 0);
  lv_obj_t *cl = lv_label_create(chip);
  lv_label_set_text(cl, TXT_ALG);
  lv_obj_set_style_text_font(cl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cl, current_theme.border_accent, 0);
  lv_obj_center(cl);

  make_kv(card, KV_KEY_K, KV_KEY_V);
  make_kv(card, KV_SES_K, KV_SES_V);
}

static void build_dump(lv_obj_t *parent) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, TXT_FILE);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, current_theme.text_secondary, 0);

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

  for (int i = 0; i < DUMP_LINE_COUNT; i++) {
    lv_obj_t *ln = lv_label_create(box);
    lv_label_set_recolor(ln, true);
    lv_label_set_text(ln, DUMP_LINES[i]);
    lv_obj_set_style_text_font(ln, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ln, lv_color_hex(COL_CYAN), 0);
  }
}

static void nfc_desfire_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    default:
      break;
  }
}

void ui_nfc_desfire_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

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
  lv_obj_set_size(body, ui_screen_w(), BODY_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, MX, 0);
  lv_obj_set_style_pad_row(body, ROW_GAP, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  build_auth_card(body);
  build_dump(body);

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(nfc_desfire_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

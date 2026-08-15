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

#include "nfc_bankcard_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define MX        8
#define BODY_H    (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define CONTENT_W (LCD_H_RES - 2 * MX)
#define ROW_GAP   6

#define CARD_H     120
#define CARD_RAD   12
#define CARD_PAD_X 14

#define CHIP_X 14
#define CHIP_Y 40
#define CHIP_W 26
#define CHIP_H 20

#define PAN_Y  66
#define VISA_X (-12)
#define VISA_Y 12
#define EDGE_Y (-12)

#define COL_DIM   0x8A8594
#define COL_WHITE 0xFFFFFF
#define COL_GOLD  0xD9A521
#define CARD_TOP  0x3A2F6A
#define CARD_BOT  0x211A4E

#define HDR_TITLE   "BANK CARD"
#define HDR_ICON    "/assets/icons/nfc.bin"
#define FOOTER_HINT "OK read   UP/DOWN aid   BACK"

#define TXT_VISA   "VISA"
#define TXT_PAN    "4085 **** **** 6027"
#define TXT_VALID  "VALID THRU 08/27"
#define TXT_CREDIT "CREDIT"

#define KV_APP_K "App label"
#define KV_APP_V "VISA CREDIT"
#define KV_AID_K "AID"
#define KV_AID_V "A0000000031010"
#define KV_CTY_K "Country"
#define KV_CTY_V "076 - BRL"

static lv_obj_t *s_screen = NULL;

static void make_kv(lv_obj_t *parent, const char *k, const char *v, lv_color_t vcol) {
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
  lv_obj_set_style_text_color(kl, lv_color_hex(COL_DIM), 0);

  lv_obj_t *vl = lv_label_create(row);
  lv_label_set_text(vl, v);
  lv_obj_set_style_text_font(vl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(vl, vcol, 0);
}

static void build_card(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, CARD_H);
  lv_obj_set_style_radius(card, CARD_RAD, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(CARD_TOP), 0);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(CARD_BOT), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 16, 0);
  lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);

  lv_obj_t *visa = lv_label_create(card);
  lv_label_set_text(visa, TXT_VISA);
  lv_obj_set_style_text_font(visa, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(visa, lv_color_hex(COL_WHITE), 0);
  lv_obj_align(visa, LV_ALIGN_TOP_RIGHT, VISA_X, VISA_Y);

  lv_obj_t *chip = lv_obj_create(card);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, CHIP_W, CHIP_H);
  lv_obj_align(chip, LV_ALIGN_TOP_LEFT, CHIP_X, CHIP_Y);
  lv_obj_set_style_radius(chip, 3, 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(COL_GOLD), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_80, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_set_style_pad_all(chip, 0, 0);

  lv_obj_t *pan = lv_label_create(card);
  lv_label_set_text(pan, TXT_PAN);
  lv_obj_set_style_text_font(pan, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(pan, lv_color_hex(COL_WHITE), 0);
  lv_obj_align(pan, LV_ALIGN_TOP_LEFT, CHIP_X, PAN_Y);

  lv_obj_t *valid = lv_label_create(card);
  lv_label_set_text(valid, TXT_VALID);
  lv_obj_set_style_text_font(valid, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(valid, lv_color_hex(COL_WHITE), 0);
  lv_obj_set_style_text_opa(valid, LV_OPA_70, 0);
  lv_obj_align(valid, LV_ALIGN_BOTTOM_LEFT, CHIP_X, EDGE_Y);

  lv_obj_t *credit = lv_label_create(card);
  lv_label_set_text(credit, TXT_CREDIT);
  lv_obj_set_style_text_font(credit, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(credit, lv_color_hex(COL_WHITE), 0);
  lv_obj_align(credit, LV_ALIGN_BOTTOM_RIGHT, VISA_X, EDGE_Y);
}

static void nfc_bankcard_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    default:
      break;
  }
}

void ui_nfc_bankcard_open(void) {
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
  lv_obj_set_size(body, LCD_H_RES, BODY_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, MX, 0);
  lv_obj_set_style_pad_row(body, ROW_GAP, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  build_card(body);
  make_kv(body, KV_APP_K, KV_APP_V, current_theme.text_main);
  make_kv(body, KV_AID_K, KV_AID_V, current_theme.border_accent);
  make_kv(body, KV_CTY_K, KV_CTY_V, current_theme.text_main);

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(nfc_bankcard_input, NULL);

  ui_screen_load(s_screen);
}

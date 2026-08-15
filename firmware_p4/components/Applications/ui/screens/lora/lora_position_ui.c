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

#include "lora_position_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HDR_TITLE   "POSITION"
#define HDR_ICON    NULL
#define FOOTER_HINT "MOVE EDIT   OK BROADCAST   BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_H   (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)

#define ROOT_PAD 8
#define ROOT_GAP 7

#define CARD_PAD 8
#define CARD_GAP 5
#define CARD_RAD 12

#define FIELD_CNT 3
#define ROW_H     28
#define ROW_RAD   7
#define ROW_PAD_H 8
#define TAG_W     34
#define CARET_W   2
#define CARET_H   16
#define GLOW_W    10

#define BC_H     30
#define BC_RAD   8
#define BC_PAD_H 9

#define CAP_TEXT "shared with 4 peers \xE2\x80\xA2 manual fix (no GPS)"

#define COL_DIM       0x8A8594
#define COL_OK        0x00E676
#define COL_BC_BORDER 0x234A3E

static const char *F_TAG[FIELD_CNT] = {"LAT", "LON", "ALT"};
static const char *F_VAL[FIELD_CNT] = {
    "-23.5614\xC2\xB0",
    "-46.6558\xC2\xB0",
    "812 m",
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_row[FIELD_CNT];
static lv_obj_t *s_tag[FIELD_CNT];
static lv_obj_t *s_caret[FIELD_CNT];
static lv_obj_t *s_bc_row = NULL;
static lv_obj_t *s_bc_val = NULL;

static int s_field = 1;
static bool s_bcast = true;

static void make_field(lv_obj_t *card, int i) {
  lv_obj_t *row = lv_obj_create(card);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, lv_pct(100), ROW_H);
  lv_obj_set_style_radius(row, ROW_RAD, 0);
  lv_obj_set_style_pad_hor(row, ROW_PAD_H, 0);
  lv_obj_set_style_pad_ver(row, 0, 0);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *tag = lv_label_create(row);
  lv_label_set_text(tag, F_TAG[i]);
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
  lv_obj_set_width(tag, TAG_W);

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, F_VAL[i]);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(val, current_theme.text_main, 0);
  lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_flex_grow(val, 1);

  lv_obj_t *caret = lv_obj_create(row);
  lv_obj_remove_flag(caret, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(caret, CARET_W, CARET_H);
  lv_obj_set_style_radius(caret, 0, 0);
  lv_obj_set_style_border_width(caret, 0, 0);
  lv_obj_set_style_bg_color(caret, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);

  s_row[i] = row;
  s_tag[i] = tag;
  s_caret[i] = caret;
}

static void refresh_fields(void) {
  for (int i = 0; i < FIELD_CNT; i++) {
    bool act = (i == s_field);
    lv_obj_set_style_bg_color(
        s_row[i], act ? current_theme.bg_secondary : current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(s_row[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(
        s_row[i], act ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_color(s_row[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(s_row[i], act ? GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_row[i], act ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(
        s_tag[i], act ? current_theme.border_accent : lv_color_hex(COL_DIM), 0);
    if (act)
      lv_obj_remove_flag(s_caret[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_caret[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void refresh_broadcast(void) {
  if (s_bcast) {
    lv_label_set_text(s_bc_val, "ON \xE2\x80\xA2 30s");
    lv_obj_set_style_text_color(s_bc_val, lv_color_hex(COL_OK), 0);
    lv_obj_set_style_border_color(s_bc_row, lv_color_hex(COL_BC_BORDER), 0);
  } else {
    lv_label_set_text(s_bc_val, "OFF");
    lv_obj_set_style_text_color(s_bc_val, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_border_color(s_bc_row, current_theme.border_inactive, 0);
  }
}

static void lora_position_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_LORA_CHAT);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_field = (s_field + 1) % FIELD_CNT;
        refresh_fields();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_field = (s_field - 1 + FIELD_CNT) % FIELD_CNT;
        refresh_fields();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        s_bcast = !s_bcast;
        refresh_broadcast();
        ui_feedback(UI_FB_SELECT);
      }
      break;
    default:
      break;
  }
}

void ui_lora_position_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_field = 1;
  s_bcast = true;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *root = lv_obj_create(s_screen);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(root, LCD_H_RES, BODY_H);
  lv_obj_align(root, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, ROOT_PAD, 0);
  lv_obj_set_style_pad_row(root, ROOT_GAP, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_radius(card, CARD_RAD, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, CARD_PAD, 0);
  lv_obj_set_style_pad_row(card, CARD_GAP, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *cap = lv_label_create(card);
  lv_label_set_text(cap, "FIXED POSITION");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_DIM), 0);

  for (int i = 0; i < FIELD_CNT; i++)
    make_field(card, i);

  s_bc_row = lv_obj_create(root);
  lv_obj_remove_flag(s_bc_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_bc_row, lv_pct(100), BC_H);
  lv_obj_set_style_radius(s_bc_row, BC_RAD, 0);
  lv_obj_set_style_bg_color(s_bc_row, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(s_bc_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_bc_row, 1, 0);
  lv_obj_set_style_pad_hor(s_bc_row, BC_PAD_H, 0);
  lv_obj_set_style_pad_ver(s_bc_row, 0, 0);
  lv_obj_set_style_pad_column(s_bc_row, 6, 0);
  lv_obj_set_flex_flow(s_bc_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_bc_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *bc_ic = lv_label_create(s_bc_row);
  lv_label_set_text(bc_ic, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(bc_ic, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bc_ic, current_theme.text_main, 0);

  lv_obj_t *bc_lbl = lv_label_create(s_bc_row);
  lv_label_set_text(bc_lbl, "Broadcast");
  lv_obj_set_style_text_font(bc_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(bc_lbl, current_theme.text_main, 0);
  lv_obj_set_flex_grow(bc_lbl, 1);

  s_bc_val = lv_label_create(s_bc_row);
  lv_obj_set_style_text_font(s_bc_val, &lv_font_montserrat_12, 0);

  lv_obj_t *cap2 = lv_label_create(root);
  lv_label_set_long_mode(cap2, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(cap2, lv_pct(100));
  lv_label_set_text(cap2, CAP_TEXT);
  lv_obj_set_style_text_font(cap2, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap2, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_align(cap2, LV_TEXT_ALIGN_CENTER, 0);

  refresh_fields();
  refresh_broadcast();

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(lora_position_input, NULL);

  ui_screen_load(s_screen);
}

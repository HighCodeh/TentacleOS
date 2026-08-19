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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "st7789.h"

#include "keyboard_ui.h"
#include "lora_session.h"
#include "meshcore.h"
#include "mt_mod_position.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define HDR_TITLE   "POSITION"
#define HDR_ICON    NULL
#define FOOTER_HINT "MOVE  " LV_SYMBOL_RIGHT " EDIT   OK BCAST   BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_H   (ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)

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

#define COL_BC_BORDER 0x234A3E

#define LAT_E7_LIMIT   900000000
#define LON_E7_LIMIT   1800000000
#define ALT_M_MIN      (-1000)
#define ALT_M_MAX      100000
#define COORD_E7_SCALE 10000000
#define COORD_FRAC_DIV 1000

static const char *F_TAG[FIELD_CNT] = {"LAT", "LON", "ALT"};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_row[FIELD_CNT];
static lv_obj_t *s_tag[FIELD_CNT];
static lv_obj_t *s_val[FIELD_CNT];
static lv_obj_t *s_caret[FIELD_CNT];
static lv_obj_t *s_bc_row = NULL;
static lv_obj_t *s_bc_val = NULL;

static lora_proto_t s_proto = LORA_PROTO_NONE;
static int s_field = 0;
static bool s_bcast = false;

static int32_t s_lat_e7 = 0;
static int32_t s_lon_e7 = 0;
static int32_t s_alt_m = 0;

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static int32_t parse_e7(const char *s) {
  bool neg = false;
  long long ip = 0;
  long long fp = 0;
  int fdig = 0;
  const char *p = s;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == '+') {
    p++;
  } else if (*p == '-') {
    neg = true;
    p++;
  }
  while (*p >= '0' && *p <= '9') {
    if (ip < 100000)
      ip = ip * 10 + (*p - '0');
    p++;
  }
  if (*p == '.' || *p == ',') {
    p++;
    while (*p >= '0' && *p <= '9' && fdig < 7) {
      fp = fp * 10 + (*p - '0');
      fdig++;
      p++;
    }
  }
  while (fdig < 7) {
    fp *= 10;
    fdig++;
  }
  long long e7 = ip * (long long)COORD_E7_SCALE + fp;
  if (neg)
    e7 = -e7;
  if (e7 > 2000000000LL)
    e7 = 2000000000LL;
  if (e7 < -2000000000LL)
    e7 = -2000000000LL;
  return (int32_t)e7;
}

static int32_t parse_int(const char *s) {
  bool neg = false;
  long long v = 0;
  const char *p = s;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == '+') {
    p++;
  } else if (*p == '-') {
    neg = true;
    p++;
  }
  while (*p >= '0' && *p <= '9') {
    if (v < 100000000LL)
      v = v * 10 + (*p - '0');
    p++;
  }
  if (neg)
    v = -v;
  return (int32_t)v;
}

static void fmt_latlon(int32_t e7, char *buf, size_t n) {
  int32_t v = e7;
  const char *sign = "";
  if (v < 0) {
    sign = "-";
    v = -v;
  }
  int32_t deg = v / COORD_E7_SCALE;
  int32_t frac = (v % COORD_E7_SCALE) / COORD_FRAC_DIV;
  snprintf(buf, n, "%s%ld.%04ld\xC2\xB0", sign, (long)deg, (long)frac);
}

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
  lv_label_set_text(val, "--");
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
  s_val[i] = val;
  s_caret[i] = caret;
}

static void refresh_fields(void) {
  for (int i = 0; i < FIELD_CNT; i++) {
    if (s_row[i] == NULL)
      continue;
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
        s_tag[i], act ? current_theme.border_accent : current_theme.text_secondary, 0);
    if (act)
      lv_obj_remove_flag(s_caret[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_caret[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void refresh_values(void) {
  char buf[32];
  if (s_val[0] != NULL) {
    fmt_latlon(s_lat_e7, buf, sizeof(buf));
    lv_label_set_text(s_val[0], buf);
  }
  if (s_val[1] != NULL) {
    fmt_latlon(s_lon_e7, buf, sizeof(buf));
    lv_label_set_text(s_val[1], buf);
  }
  if (s_val[2] != NULL) {
    snprintf(buf, sizeof(buf), "%ld m", (long)s_alt_m);
    lv_label_set_text(s_val[2], buf);
  }
}

static void refresh_broadcast(void) {
  if (s_bc_val == NULL || s_bc_row == NULL)
    return;
  if (s_bcast) {
    lv_label_set_text(s_bc_val, "ON");
    lv_obj_set_style_text_color(s_bc_val, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_obj_set_style_border_color(s_bc_row, lv_color_hex(COL_BC_BORDER), 0);
  } else {
    lv_label_set_text(s_bc_val, "OFF");
    lv_obj_set_style_text_color(s_bc_val, current_theme.text_secondary, 0);
    lv_obj_set_style_border_color(s_bc_row, current_theme.border_inactive, 0);
  }
}

static void apply_position(void) {
  if (s_proto == LORA_PROTO_MESHTASTIC) {
    mt_mod_position_set_fixed(s_lat_e7, s_lon_e7, s_alt_m);
  } else if (s_proto == LORA_PROTO_MESHCORE) {
    meshcore_set_advert_latlon(s_lat_e7 / 10, s_lon_e7 / 10, true);
  }
}

static void clear_position(void) {
  if (s_proto == LORA_PROTO_MESHTASTIC) {
    mt_mod_position_remove_fixed();
  } else if (s_proto == LORA_PROTO_MESHCORE) {
    meshcore_set_advert_latlon(0, 0, false);
  }
}

static void on_kb_submit(const char *text, void *user_data) {
  int field = (int)(intptr_t)user_data;
  if (text == NULL || text[0] == '\0')
    return;
  if (field < 0 || field >= FIELD_CNT)
    return;
  if (field == 0) {
    s_lat_e7 = clamp_i32(parse_e7(text), -LAT_E7_LIMIT, LAT_E7_LIMIT);
  } else if (field == 1) {
    s_lon_e7 = clamp_i32(parse_e7(text), -LON_E7_LIMIT, LON_E7_LIMIT);
  } else {
    s_alt_m = clamp_i32(parse_int(text), ALT_M_MIN, ALT_M_MAX);
  }
  refresh_values();
  ui_feedback(UI_FB_WRITE);
  if (s_bcast)
    apply_position();
}

static void lora_position_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_proto == LORA_PROTO_NONE) {
    if ((ev->button == INPUT_BTN_BACK || ev->button == INPUT_BTN_LEFT) && press)
      ui_switch_screen(SCREEN_LORA_CHAT);
    return;
  }

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
    case INPUT_BTN_RIGHT:
      if (press) {
        if (s_field < 0 || s_field >= FIELD_CNT)
          s_field = 0;
        keyboard_open(NULL, on_kb_submit, (void *)(intptr_t)s_field);
        ui_feedback(UI_FB_SELECT);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        s_bcast = !s_bcast;
        if (s_bcast) {
          apply_position();
          notify(NOTIFY_LORA, "Position broadcasting");
        } else {
          clear_position();
          notify(NOTIFY_LORA, "Position cleared");
        }
        refresh_broadcast();
        ui_feedback(UI_FB_SELECT);
      }
      break;
    default:
      break;
  }
}

static void build_placeholder(void) {
  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *msg = lv_label_create(s_screen);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg, ui_screen_w() - 2 * ROOT_PAD);
  lv_label_set_text(msg, "Start a protocol first");
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(msg, current_theme.text_secondary, 0);
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(msg);

  ui_chrome_footer(s_screen, "BACK");
}

void ui_lora_position_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  for (int i = 0; i < FIELD_CNT; i++) {
    s_row[i] = NULL;
    s_tag[i] = NULL;
    s_val[i] = NULL;
    s_caret[i] = NULL;
  }
  s_bc_row = NULL;
  s_bc_val = NULL;

  s_proto = lora_session_active();
  s_field = 0;
  s_bcast = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  if (s_proto == LORA_PROTO_NONE) {
    build_placeholder();
    ui_input_set_screen_handler(lora_position_input, NULL);
    ui_screen_load_owned(&s_screen, s_screen);
    return;
  }

  if (s_proto == LORA_PROTO_MESHCORE) {
    int32_t lat_e6 = 0;
    int32_t lon_e6 = 0;
    bool has = false;
    meshcore_get_advert_latlon(&lat_e6, &lon_e6, &has);
    s_lat_e7 = clamp_i32((int32_t)((int64_t)lat_e6 * 10), -LAT_E7_LIMIT, LAT_E7_LIMIT);
    s_lon_e7 = clamp_i32((int32_t)((int64_t)lon_e6 * 10), -LON_E7_LIMIT, LON_E7_LIMIT);
    s_bcast = has;
  }

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *root = lv_obj_create(s_screen);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(root, ui_screen_w(), BODY_H);
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
  lv_obj_set_style_text_color(cap, current_theme.text_secondary, 0);

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
  lv_label_set_text(cap2,
                    (s_proto == LORA_PROTO_MESHTASTIC)
                        ? "manual fix (no GPS) \xE2\x80\xA2 rebroadcast every 15 min"
                        : "manual fix (no GPS) \xE2\x80\xA2 sent in each advert");
  lv_obj_set_style_text_font(cap2, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap2, current_theme.text_secondary, 0);
  lv_obj_set_style_text_align(cap2, LV_TEXT_ALIGN_CENTER, 0);

  refresh_fields();
  refresh_values();
  refresh_broadcast();

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(lora_position_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

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

#include "nfc_p2p_ui.h"

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
#define ROW_GAP   8

#define DEV_ROW_H 62
#define DEV_W     34
#define DEV_H     44
#define DEV_RAD   6
#define DEV_COL_W 58

#define CONN_W   70
#define CONN_H   DEV_H
#define CONN_MID 22
#define DOT_R    3

#define STEP_MARK_W 16
#define STEP_GAP    4

#define STREAM_MS    220
#define STREAM_START 62
#define STREAM_STEP  4
#define STREAM_MAX   100

#define COL_DIM  0x8A8594
#define COL_OK   0x00E676
#define COL_CYAN 0x37E0A8
#define COL_LINE 0x2A2636

#define PCT_BUF 16

#define HDR_TITLE   "SHARE (P2P)"
#define HDR_ICON    "/assets/icons/nfc.bin"
#define FOOTER_HINT "OK send   UP/DOWN payload   BACK"

#define DEV_LOCAL  "HB"
#define DEV_REMOTE LV_SYMBOL_CALL
#define LBL_LOCAL  "HighBoy"
#define LBL_REMOTE "Pixel 8"

#define STEP1_TXT  "LLCP link"
#define STEP1_META "ATR ok"
#define STEP2_TXT  "SNEP connect"
#define STEP2_META "port 04"
#define STEP3_TXT  "PUT ndef"
#define STEP_DONE  "done"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_put_mark = NULL;
static lv_obj_t *s_put_pct = NULL;
static lv_timer_t *s_stream_timer = NULL;
static lv_point_precise_t s_link_pts[2] = {{2, CONN_MID}, {CONN_W - 2, CONN_MID}};

static int s_pct = STREAM_START;

static lv_obj_t *make_device(lv_obj_t *parent, const char *glyph, const char *name, bool active) {
  lv_obj_t *col = lv_obj_create(parent);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(col, DEV_COL_W, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, 3, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *box = lv_obj_create(col);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(box, DEV_W, DEV_H);
  lv_obj_set_style_radius(box, DEV_RAD, 0);
  lv_obj_set_style_bg_color(box, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(
      box, active ? current_theme.border_accent : lv_color_hex(COL_LINE), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_pad_all(box, 0, 0);

  lv_obj_t *g = lv_label_create(box);
  lv_label_set_text(g, glyph);
  lv_obj_set_style_text_font(g, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g, active ? current_theme.border_accent : current_theme.text_main, 0);
  lv_obj_center(g);

  lv_obj_t *nm = lv_label_create(col);
  lv_label_set_text(nm, name);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(nm, lv_color_hex(COL_DIM), 0);
  return col;
}

static void make_connector(lv_obj_t *parent) {
  lv_obj_t *conn = lv_obj_create(parent);
  lv_obj_remove_flag(conn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(conn, CONN_W, CONN_H);
  lv_obj_set_style_bg_opa(conn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(conn, 0, 0);
  lv_obj_set_style_pad_all(conn, 0, 0);

  lv_obj_t *line = lv_line_create(conn);
  lv_line_set_points(line, s_link_pts, 2);
  lv_obj_set_style_line_color(line, current_theme.border_accent, 0);
  lv_obj_set_style_line_width(line, 2, 0);
  lv_obj_set_style_line_dash_width(line, 4, 0);
  lv_obj_set_style_line_dash_gap(line, 3, 0);

  const int dot_x[3] = {22, 40, 58};
  const lv_color_t dot_c[3] = {
      current_theme.border_accent, lv_color_hex(COL_CYAN), current_theme.border_accent};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *dot = lv_obj_create(conn);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, DOT_R * 2, DOT_R * 2);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, dot_x[i] - DOT_R, CONN_MID - CONN_H / 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, dot_c[i], 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  }
}

static void make_step(lv_obj_t *parent,
                      const char *mark,
                      lv_color_t mark_col,
                      const char *txt,
                      const char *meta,
                      lv_color_t meta_col,
                      lv_obj_t **out_mark,
                      lv_obj_t **out_meta) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, STEP_GAP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *m = lv_label_create(row);
  lv_label_set_text(m, mark);
  lv_obj_set_style_text_font(m, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(m, mark_col, 0);
  lv_obj_set_width(m, STEP_MARK_W);
  lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *t = lv_label_create(row);
  lv_label_set_text(t, txt);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);
  lv_obj_set_flex_grow(t, 1);

  lv_obj_t *me = lv_label_create(row);
  lv_label_set_text(me, meta);
  lv_obj_set_style_text_font(me, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(me, meta_col, 0);

  if (out_mark)
    *out_mark = m;
  if (out_meta)
    *out_meta = me;
}

static void set_pct_text(void) {
  static char buf[PCT_BUF];
  snprintf(buf, sizeof(buf), "%d%%", s_pct);
  lv_label_set_text(s_put_pct, buf);
}

static void stream_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_stream_timer = NULL;
    return;
  }
  s_pct += STREAM_STEP;
  if (s_pct >= STREAM_MAX) {
    s_pct = STREAM_MAX;
    lv_label_set_text(s_put_mark, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(s_put_mark, lv_color_hex(COL_OK), 0);
    lv_label_set_text(s_put_pct, STEP_DONE);
    lv_obj_set_style_text_color(s_put_pct, lv_color_hex(COL_OK), 0);
    lv_timer_delete(t);
    s_stream_timer = NULL;
    return;
  }
  set_pct_text();
}

static void build_body(lv_obj_t *parent) {
  lv_obj_t *devrow = lv_obj_create(parent);
  lv_obj_remove_flag(devrow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(devrow, lv_pct(100));
  lv_obj_set_height(devrow, DEV_ROW_H);
  lv_obj_set_style_bg_opa(devrow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(devrow, 0, 0);
  lv_obj_set_style_pad_all(devrow, 0, 0);
  lv_obj_set_flex_flow(devrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      devrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  make_device(devrow, DEV_LOCAL, LBL_LOCAL, true);
  make_connector(devrow);
  make_device(devrow, DEV_REMOTE, LBL_REMOTE, false);

  lv_obj_t *steps = lv_obj_create(parent);
  lv_obj_remove_flag(steps, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(steps, lv_pct(100));
  lv_obj_set_height(steps, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(steps, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(steps, 0, 0);
  lv_obj_set_style_pad_all(steps, 0, 0);
  lv_obj_set_style_pad_row(steps, STEP_GAP, 0);
  lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_COLUMN);

  make_step(steps,
            LV_SYMBOL_OK,
            lv_color_hex(COL_OK),
            STEP1_TXT,
            STEP1_META,
            lv_color_hex(COL_DIM),
            NULL,
            NULL);
  make_step(steps,
            LV_SYMBOL_OK,
            lv_color_hex(COL_OK),
            STEP2_TXT,
            STEP2_META,
            lv_color_hex(COL_DIM),
            NULL,
            NULL);
  make_step(steps,
            LV_SYMBOL_RIGHT,
            current_theme.border_accent,
            STEP3_TXT,
            "",
            current_theme.border_accent,
            &s_put_mark,
            &s_put_pct);
  set_pct_text();
}

static void nfc_p2p_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_OK:
      if (press)
        ui_feedback(UI_FB_SELECT);
      break;
    default:
      break;
  }
}

void ui_nfc_p2p_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  if (s_stream_timer != NULL) {
    lv_timer_delete(s_stream_timer);
    s_stream_timer = NULL;
  }
  s_pct = STREAM_START;

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

  ui_input_set_screen_handler(nfc_p2p_input, NULL);
  s_stream_timer = lv_timer_create(stream_cb, STREAM_MS, NULL);

  ui_screen_load(s_screen);
}

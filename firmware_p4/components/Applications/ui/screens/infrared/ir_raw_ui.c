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

#include "ir_raw_ui.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ir_store.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50

#define HDR_TITLE "RAW SIGNAL"
#define HDR_ICON  "/assets/icons/graphic_eq.bin"
#define FOOTER    "OK replay   BACK exit"

#define MX        8
#define CONTENT_W (LCD_H_RES - 2 * MX)

#define CARD1_Y     50
#define CARD1_H     76
#define CARD_RADIUS 12
#define CARD_PAD_X  9
#define CARD_HDR_Y  8
#define SCOPE_X     CARD_PAD_X
#define SCOPE_Y     24
#define DOT_SIZE    7

#define GRID_Y     (CARD1_Y + CARD1_H + 8)
#define G_TILE_W   70
#define G_TILE_H   46
#define G_GAP      7
#define G_TILE_PAD 7
#define G_CAP_Y    6
#define G_VAL_Y    22
#define G_RADIUS   8

#define CAR_LBL_Y (GRID_Y + G_TILE_H + 8)
#define CHIP_Y    (CAR_LBL_Y + 20)
#define CHIP_H    24
#define CHIP_GAP  6
#define CHIP_PAD  10
#define CHIP_RAD  8
#define KV_Y      (CHIP_Y + CHIP_H + 8)

#define COL_DIM 0x8A8594
#define COL_OK  0x00E676

#define CARRIER_COUNT 4
#define CHIP_TXT_LEN  10

static const int G_TILE_X[3] = {MX, MX + G_TILE_W + G_GAP, MX + 2 * (G_TILE_W + G_GAP)};

typedef struct {
  const char *cap;
  const char *unit;
} stat_t;

static const stat_t STATS[3] = {
    {"PULSES", NULL},
    {"LENGTH", "ms"},
    {"PEAK", "ms"},
};
static char s_stat_vals[3][12];

static const char *CARRIERS[CARRIER_COUNT] = {"36", "37", "38", "40"};

static const lv_point_precise_t PULSE_PTS[] = {
    {0, 38},   {6, 38},   {6, 6},   {14, 6},  {14, 38},  {40, 38},  {40, 6},  {46, 6},
    {46, 38},  {54, 38},  {54, 6},  {60, 6},  {60, 38},  {84, 38},  {84, 6},  {90, 6},
    {90, 38},  {98, 38},  {98, 6},  {104, 6}, {104, 38}, {128, 38}, {128, 6}, {134, 6},
    {134, 38}, {142, 38}, {142, 6}, {148, 6}, {148, 38}, {172, 38}, {172, 6}, {178, 6},
    {178, 38}, {186, 38}, {186, 6}, {192, 6}, {192, 38}, {200, 38}};
#define PULSE_PT_COUNT ((int)(sizeof(PULSE_PTS) / sizeof(PULSE_PTS[0])))

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_chip[CARRIER_COUNT];
static lv_obj_t *s_chip_lbl[CARRIER_COUNT];
static lv_timer_t *s_nav_timer = NULL;

static int s_sel = 2;

static rmt_symbol_word_t s_raw[IR_MAX_SYMBOLS];
static size_t s_raw_count = 0;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_right_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

// Pull the last frame the RMT RX captured and derive real scope stats from it.
static void compute_stats(void) {
  s_raw_count = 0;
  if (ir_rx_init() == ESP_OK)
    ir_get_last_raw(s_raw, IR_MAX_SYMBOLS, &s_raw_count);

  uint32_t total = 0, peak = 0;
  for (size_t i = 0; i < s_raw_count; i++) {
    uint32_t d0 = s_raw[i].duration0;
    uint32_t d1 = s_raw[i].duration1;
    total += d0 + d1;
    if (d0 > peak)
      peak = d0;
    if (d1 > peak)
      peak = d1;
  }
  snprintf(s_stat_vals[0], sizeof(s_stat_vals[0]), "%u", (unsigned)s_raw_count);
  snprintf(s_stat_vals[1], sizeof(s_stat_vals[1]), "%u", (unsigned)(total / 1000));
  snprintf(s_stat_vals[2],
           sizeof(s_stat_vals[2]),
           "%u.%u",
           (unsigned)(peak / 1000),
           (unsigned)((peak % 1000) / 100));
}

static void build_scope_card(void) {
  bool has = s_raw_count > 0;

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, CARD1_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CARD1_Y);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);

  lv_obj_t *hdr = lv_label_create(card);
  lv_label_set_text(hdr, "PULSE TRAIN");
  lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(hdr, lv_color_hex(COL_DIM), 0);
  lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, CARD_PAD_X, CARD_HDR_Y);

  lv_obj_t *cap_grp = lv_obj_create(card);
  lv_obj_remove_flag(cap_grp, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cap_grp, LV_SIZE_CONTENT, 16);
  lv_obj_align(cap_grp, LV_ALIGN_TOP_RIGHT, -CARD_PAD_X, CARD_HDR_Y);
  lv_obj_set_style_bg_opa(cap_grp, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cap_grp, 0, 0);
  lv_obj_set_style_pad_all(cap_grp, 0, 0);
  lv_obj_set_style_pad_column(cap_grp, 4, 0);
  lv_obj_set_flex_flow(cap_grp, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cap_grp, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *dot = lv_obj_create(cap_grp);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(has ? COL_OK : COL_DIM), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

  lv_obj_t *cap_lbl = lv_label_create(cap_grp);
  lv_label_set_text(cap_lbl, has ? "CAPTURED" : "EMPTY");
  lv_obj_set_style_text_font(cap_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap_lbl, lv_color_hex(has ? COL_OK : COL_DIM), 0);

  lv_obj_t *scope = lv_line_create(card);
  lv_line_set_points(scope, PULSE_PTS, PULSE_PT_COUNT);
  lv_obj_set_pos(scope, SCOPE_X, SCOPE_Y);
  lv_obj_set_style_line_color(scope, current_theme.border_accent, 0);
  lv_obj_set_style_line_opa(scope, has ? LV_OPA_COVER : LV_OPA_40, 0);
  lv_obj_set_style_line_width(scope, 2, 0);
  lv_obj_set_style_line_rounded(scope, false, 0);
}

static void build_stat_tile(int i) {
  lv_obj_t *tile = lv_obj_create(s_screen);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(tile, G_TILE_W, G_TILE_H);
  lv_obj_set_pos(tile, G_TILE_X[i], GRID_Y);
  lv_obj_set_style_radius(tile, G_RADIUS, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_set_style_bg_color(tile, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(tile, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(tile, 1, 0);

  lv_obj_t *cap = lv_label_create(tile);
  lv_label_set_text(cap, STATS[i].cap);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_DIM), 0);
  lv_obj_align(cap, LV_ALIGN_TOP_LEFT, G_TILE_PAD, G_CAP_Y);

  lv_obj_t *grp = lv_obj_create(tile);
  lv_obj_remove_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grp, G_TILE_W - 2 * G_TILE_PAD, 22);
  lv_obj_align(grp, LV_ALIGN_TOP_LEFT, G_TILE_PAD, G_VAL_Y);
  lv_obj_set_style_bg_opa(grp, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grp, 0, 0);
  lv_obj_set_style_pad_all(grp, 0, 0);
  lv_obj_set_style_pad_column(grp, 3, 0);
  lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *val = lv_label_create(grp);
  lv_label_set_text(val, s_stat_vals[i]);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(val, current_theme.text_main, 0);

  if (STATS[i].unit != NULL) {
    lv_obj_t *unit = lv_label_create(grp);
    lv_label_set_text(unit, STATS[i].unit);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(COL_DIM), 0);
  }
}

static void style_chip(int i, bool sel) {
  char buf[CHIP_TXT_LEN];
  if (sel) {
    lv_snprintf(buf, sizeof(buf), "%s kHz", CARRIERS[i]);
    lv_label_set_text(s_chip_lbl[i], buf);
    lv_obj_set_style_bg_color(s_chip[i], current_theme.border_accent, 0);
    lv_obj_set_style_bg_opa(s_chip[i], LV_OPA_20, 0);
    lv_obj_set_style_border_color(s_chip[i], current_theme.border_accent, 0);
    lv_obj_set_style_text_color(s_chip_lbl[i], current_theme.border_accent, 0);
  } else {
    lv_label_set_text(s_chip_lbl[i], CARRIERS[i]);
    lv_obj_set_style_bg_color(s_chip[i], current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(s_chip[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_chip[i], current_theme.border_inactive, 0);
    lv_obj_set_style_text_color(s_chip_lbl[i], lv_color_hex(COL_DIM), 0);
  }
}

static void build_carrier(void) {
  lv_obj_t *lbl = lv_label_create(s_screen);
  lv_label_set_text(lbl, "CARRIER FREQUENCY");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, MX, CAR_LBL_Y);

  lv_obj_t *row = lv_obj_create(s_screen);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, CONTENT_W, CHIP_H);
  lv_obj_align(row, LV_ALIGN_TOP_LEFT, MX, CHIP_Y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, CHIP_GAP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < CARRIER_COUNT; i++) {
    lv_obj_t *chip = lv_obj_create(row);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, CHIP_H);
    lv_obj_set_style_radius(chip, CHIP_RAD, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_hor(chip, CHIP_PAD, 0);
    lv_obj_set_style_pad_ver(chip, 0, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_chip[i] = chip;

    lv_obj_t *ct = lv_label_create(chip);
    lv_obj_set_style_text_font(ct, &lv_font_montserrat_12, 0);
    s_chip_lbl[i] = ct;

    style_chip(i, i == s_sel);
  }
}

static void build_saved_row(void) {
  lv_obj_t *tag = lv_label_create(s_screen);
  lv_label_set_text(tag, "Last capture");
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tag, lv_color_hex(COL_DIM), 0);
  lv_obj_align(tag, LV_ALIGN_TOP_LEFT, MX, KV_Y);

  lv_obj_t *name = lv_label_create(s_screen);
  if (s_raw_count > 0)
    lv_label_set_text_fmt(name, "%u pulses", (unsigned)s_raw_count);
  else
    lv_label_set_text(name, "none");
  lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(name, current_theme.border_accent, 0);
  lv_obj_align(name, LV_ALIGN_TOP_RIGHT, -MX, KV_Y);
}

static void refresh_selection(void) {
  for (int i = 0; i < CARRIER_COUNT; i++)
    style_chip(i, i == s_sel);
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
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_IR_MENU);
    return;
  }
  if ((right && !s_right_last) || (down && !s_down_last)) {
    s_sel = (s_sel + 1) % CARRIER_COUNT;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
  if ((left && !s_left_last) || (up && !s_up_last)) {
    s_sel = (s_sel - 1 + CARRIER_COUNT) % CARRIER_COUNT;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (ok && !s_ok_last) {
    if (s_raw_count > 0) {
      uint32_t hz = (uint32_t)atoi(CARRIERS[s_sel]) * 1000;
      ir_store_send_raw(s_raw, s_raw_count, hz);
      notify(NOTIFY_INFO, "Replaying RAW signal");
      ui_feedback(UI_FB_EMULATE);
    } else {
      notify(NOTIFY_WARNING, "No RAW signal captured");
    }
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_ir_raw_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 2;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;

  compute_stats();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_scope_card();
  for (int i = 0; i < 3; i++)
    build_stat_tile(i);
  build_carrier();
  build_saved_row();

  ui_chrome_footer(s_screen, FOOTER);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

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

#include "lora_telemetry_ui.h"

#include <stdio.h>

#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50
#define SPARK_MS     420

#define HDR_TITLE   "TELEMETRY"
#define HDR_ICON    NULL
#define FOOTER_HINT "UP/DOWN   OK NODE   BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_H   (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)

#define ROOT_PAD 8
#define ROOT_GAP 7

#define PANEL_RAD   8
#define PANEL_PAD_H 7
#define PANEL_PAD_T 6
#define PANEL_PAD_B 4
#define PANEL_GAP   2

#define SPARK_W   200
#define SPARK_H   28
#define SPARK_PTS 13
#define SPARK_MG  2

#define BATT_H  20
#define BAR_H   8
#define BAR_RAD 4

#define NB_CNT     3
#define CHIP_H     22
#define CHIP_RAD   10
#define CHIP_PAD_H 8
#define CHIP_GAP   5
#define GLOW_W     10

#define COL_ACC2  0xB89AFF
#define COL_CYAN  0x37E0A8
#define COL_DIM   0x8A8594
#define COL_OK    0x00E676
#define COL_WARN  0xFFC23D
#define COL_BAD   0xFF5470
#define COL_TRACK 0x241F31

static const uint8_t SRC[] = {
    45, 60, 38, 72, 50, 84, 42, 66, 55, 78, 40, 70, 62, 48, 80, 44, 68, 52, 76, 46, 64, 58, 74, 50,
};
#define SRC_N ((int)(sizeof(SRC) / sizeof(SRC[0])))

static const struct {
  const char *name;
  const char *snr;
  uint32_t col;
} NB[NB_CNT] = {
    {"Relay-7", "+9.5", COL_OK},
    {"BaseCamp", "+4.0", COL_WARN},
    {"K4RX", "-6.5", COL_BAD},
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_ch_line = NULL;
static lv_obj_t *s_air_line = NULL;
static lv_obj_t *s_ch_val = NULL;
static lv_obj_t *s_air_val = NULL;
static lv_obj_t *s_chips[NB_CNT];
static lv_obj_t *s_chip_name[NB_CNT];
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_spark_timer = NULL;

static uint8_t s_ch_buf[SPARK_PTS];
static uint8_t s_air_buf[SPARK_PTS];
static lv_point_precise_t s_ch_pts[SPARK_PTS];
static lv_point_precise_t s_air_pts[SPARK_PTS];
static int s_src_idx = 0;

static int s_nb = 0;
static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static int spark_x(int i) {
  return i * SPARK_W / (SPARK_PTS - 1);
}

static int spark_y(uint8_t amp) {
  if (amp > 100)
    amp = 100;
  return SPARK_MG + (100 - amp) * (SPARK_H - 2 * SPARK_MG) / 100;
}

static void rebuild_points(void) {
  for (int i = 0; i < SPARK_PTS; i++) {
    s_ch_pts[i].x = spark_x(i);
    s_ch_pts[i].y = spark_y(s_ch_buf[i]);
    s_air_pts[i].x = spark_x(i);
    s_air_pts[i].y = spark_y(s_air_buf[i]);
  }
  if (s_ch_line)
    lv_line_set_points(s_ch_line, s_ch_pts, SPARK_PTS);
  if (s_air_line)
    lv_line_set_points(s_air_line, s_air_pts, SPARK_PTS);
}

static void update_values(void) {
  uint8_t ch = s_ch_buf[SPARK_PTS - 1];
  uint8_t air = s_air_buf[SPARK_PTS - 1];
  int ch_pct = 8 + ch * 20 / 100;
  int air_tenths = 18 + air * 50 / 100;
  if (s_ch_val) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", ch_pct);
    lv_label_set_text(s_ch_val, buf);
  }
  if (s_air_val) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%d.%d%%", air_tenths / 10, air_tenths % 10);
    lv_label_set_text(s_air_val, buf);
  }
}

static void spark_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_spark_timer = NULL;
    return;
  }
  s_src_idx = (s_src_idx + 1) % SRC_N;
  for (int i = 0; i < SPARK_PTS - 1; i++) {
    s_ch_buf[i] = s_ch_buf[i + 1];
    s_air_buf[i] = s_air_buf[i + 1];
  }
  s_ch_buf[SPARK_PTS - 1] = SRC[s_src_idx];
  s_air_buf[SPARK_PTS - 1] = (uint8_t)(SRC[(s_src_idx + 11) % SRC_N] / 2 + 25);
  rebuild_points();
  update_values();
}

static lv_obj_t *make_panel(lv_obj_t *root,
                            const char *title,
                            lv_color_t line_col,
                            lv_obj_t **out_val,
                            lv_obj_t **out_line,
                            lv_point_precise_t *pts) {
  lv_obj_t *panel = lv_obj_create(root);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_radius(panel, PANEL_RAD, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_pad_left(panel, PANEL_PAD_H, 0);
  lv_obj_set_style_pad_right(panel, PANEL_PAD_H, 0);
  lv_obj_set_style_pad_top(panel, PANEL_PAD_T, 0);
  lv_obj_set_style_pad_bottom(panel, PANEL_PAD_B, 0);
  lv_obj_set_style_pad_row(panel, PANEL_GAP, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *head = lv_obj_create(panel);
  lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(head, 0, 0);
  lv_obj_set_style_pad_all(head, 0, 0);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *cap = lv_label_create(head);
  lv_label_set_text(cap, title);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_DIM), 0);

  lv_obj_t *val = lv_label_create(head);
  lv_label_set_text(val, "--");
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(val, current_theme.text_main, 0);
  *out_val = val;

  lv_obj_t *box = lv_obj_create(panel);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(box, SPARK_W, SPARK_H);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);

  lv_obj_t *line = lv_line_create(box);
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_line_set_points(line, pts, SPARK_PTS);
  lv_obj_set_style_line_width(line, 2, 0);
  lv_obj_set_style_line_color(line, line_col, 0);
  lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  *out_line = line;

  return panel;
}

static void refresh_chips(void) {
  for (int i = 0; i < NB_CNT; i++) {
    bool sel = (i == s_nb);
    lv_obj_set_style_border_color(
        s_chips[i], sel ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(s_chips[i], sel ? 2 : 1, 0);
    lv_obj_set_style_shadow_color(s_chips[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(s_chips[i], sel ? GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_chips[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_spread(s_chips[i], sel ? -2 : 0, 0);
    lv_obj_set_style_text_color(
        s_chip_name[i], sel ? current_theme.text_main : lv_color_hex(COL_DIM), 0);
  }
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
  if (down && !s_down_last) {
    s_nb = (s_nb + 1) % NB_CNT;
    refresh_chips();
    ui_feedback(UI_FB_NAV);
  }
  if (up && !s_up_last) {
    s_nb = (s_nb - 1 + NB_CNT) % NB_CNT;
    refresh_chips();
    ui_feedback(UI_FB_NAV);
  }
  if (ok && !s_ok_last)
    ui_feedback(UI_FB_SELECT);

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_lora_telemetry_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_nb = 0;
  s_src_idx = 0;
  s_up_last = s_down_last = s_left_last = s_ok_last = s_back_last = false;
  for (int i = 0; i < SPARK_PTS; i++) {
    s_ch_buf[i] = SRC[i % SRC_N];
    s_air_buf[i] = (uint8_t)(SRC[(i + 11) % SRC_N] / 2 + 25);
  }

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

  make_panel(root, "Channel util", lv_color_hex(COL_ACC2), &s_ch_val, &s_ch_line, s_ch_pts);
  make_panel(root, "Air TX util", lv_color_hex(COL_CYAN), &s_air_val, &s_air_line, s_air_pts);

  lv_obj_t *batt = lv_obj_create(root);
  lv_obj_remove_flag(batt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(batt, lv_pct(100), BATT_H);
  lv_obj_set_style_bg_opa(batt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(batt, 0, 0);
  lv_obj_set_style_pad_all(batt, 0, 0);
  lv_obj_set_style_pad_column(batt, 7, 0);
  lv_obj_set_flex_flow(batt, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(batt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *bic = lv_label_create(batt);
  lv_label_set_text(bic, LV_SYMBOL_BATTERY_3);
  lv_obj_set_style_text_font(bic, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(bic, current_theme.text_main, 0);

  lv_obj_t *bar = lv_bar_create(batt);
  lv_obj_set_height(bar, BAR_H);
  lv_obj_set_flex_grow(bar, 1);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 84, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, BAR_RAD, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(bar, lv_color_hex(COL_ACC2), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, BAR_RAD, LV_PART_INDICATOR);

  lv_obj_t *bpct = lv_label_create(batt);
  lv_label_set_text(bpct, "84%");
  lv_obj_set_style_text_font(bpct, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bpct, current_theme.text_main, 0);

  lv_obj_t *bup = lv_label_create(batt);
  lv_label_set_text(bup, "3d 14:22");
  lv_obj_set_style_text_font(bup, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bup, lv_color_hex(COL_DIM), 0);

  lv_obj_t *nlbl = lv_label_create(root);
  lv_label_set_text(nlbl, "NEIGHBORS");
  lv_obj_set_style_text_font(nlbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(nlbl, lv_color_hex(COL_DIM), 0);

  lv_obj_t *chips = lv_obj_create(root);
  lv_obj_remove_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chips, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(chips, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chips, 0, 0);
  lv_obj_set_style_pad_all(chips, 0, 0);
  lv_obj_set_style_pad_column(chips, CHIP_GAP, 0);
  lv_obj_set_style_pad_row(chips, CHIP_GAP, 0);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  for (int i = 0; i < NB_CNT; i++) {
    lv_obj_t *chip = lv_obj_create(chips);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, CHIP_H);
    lv_obj_set_style_radius(chip, CHIP_RAD, 0);
    lv_obj_set_style_bg_color(chip, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_hor(chip, CHIP_PAD_H, 0);
    lv_obj_set_style_pad_ver(chip, 0, 0);
    lv_obj_set_style_pad_column(chip, 5, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *nm = lv_label_create(chip);
    lv_label_set_text(nm, NB[i].name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
    s_chip_name[i] = nm;

    lv_obj_t *sv = lv_label_create(chip);
    lv_label_set_text(sv, NB[i].snr);
    lv_obj_set_style_text_font(sv, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sv, lv_color_hex(NB[i].col), 0);

    s_chips[i] = chip;
  }

  rebuild_points();
  update_values();
  refresh_chips();

  ui_chrome_footer(s_screen, FOOTER_HINT);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  if (s_spark_timer == NULL)
    s_spark_timer = lv_timer_create(spark_tick_cb, SPARK_MS, NULL);

  ui_screen_load(s_screen);
}

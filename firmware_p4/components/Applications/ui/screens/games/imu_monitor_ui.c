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

#include "imu_monitor_ui.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS  50
#define SCOPE_TICK_MS 60
#define PHASE_STEP    6

#define HDR_TITLE  "MOTION / IMU"
#define HDR_ICON   "/assets/icons/sensors.bin"
#define FOOTER_TXT "OK ZERO   R RATE   BACK"

#define MX        8
#define CONTENT_W (240 - 2 * MX)

#define COL_DIM  0x8A8594
#define COL_CYAN 0x37E0A8
#define COL_WARN 0xFFC23D
#define COL_ACC2 0xB89AFF
#define COL_ZERO 0x3A3350

#define ACCEL_LBL_Y  46
#define SCOPE_CARD_Y 68
#define SCOPE_CARD_H 90
#define SCOPE_PAD    6

#define WAVE_W         (CONTENT_W - 2 * SCOPE_PAD - 2)
#define WAVE_H         58
#define WAVE_N         24
#define SCOPE_CENTER_Y 29
#define Z_BASE_Y       10
#define VAL_ROW_Y      (WAVE_H + 4)

#define AMP_X  4
#define AMP_Y  15
#define AMP_Z  3
#define SPD_X  11
#define SPD_Y  7
#define SPD_Z  5
#define STEP_X 48
#define STEP_Y 62
#define STEP_Z 37

#define GYRO_LBL_Y  165
#define GYRO_CARD_Y 187
#define GYRO_CARD_H 82
#define GYRO_PAD    9

#define GY_CONTENT_W (CONTENT_W - 2 * GYRO_PAD - 2)
#define GY_LBL_W     18
#define GY_VAL_W     46
#define GY_GAP       6
#define GY_TRACK_X   (GY_LBL_W + GY_GAP)
#define GY_TRACK_W   (GY_CONTENT_W - GY_LBL_W - GY_VAL_W - 2 * GY_GAP)
#define GY_VAL_X     (GY_TRACK_X + GY_TRACK_W + GY_GAP)
#define GY_TRACK_H   8
#define GY_FILL_H    6
#define GY_ROW_STEP  22
#define GY_ROW0_Y    2

#define RATE_COUNT 4

typedef struct {
  const char *label;
  int pct;
  bool right;
  uint32_t color;
  const char *val;
} gyro_def_t;

static const gyro_def_t GYRO_ROWS[] = {
    {"gX", 12, true, 0, "+0.62"},
    {"gY", 24, false, COL_WARN, "-1.14"},
    {"gZ", 4, true, COL_ACC2, "+0.08"},
};
#define GYRO_COUNT ((int)(sizeof(GYRO_ROWS) / sizeof(GYRO_ROWS[0])))

static const int RATES[RATE_COUNT] = {104, 208, 416, 833};

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_scope_timer = NULL;

static lv_obj_t *s_xline = NULL;
static lv_obj_t *s_yline = NULL;
static lv_obj_t *s_zline = NULL;
static lv_obj_t *s_rate_lbl = NULL;
static lv_obj_t *s_gyro_fill[GYRO_COUNT];
static lv_obj_t *s_gyro_val[GYRO_COUNT];

static lv_point_precise_t s_xpts[WAVE_N];
static lv_point_precise_t s_ypts[WAVE_N];
static lv_point_precise_t s_zpts[WAVE_N];

static int s_phase = 0;
static int s_rate_idx = RATE_COUNT - 1;
static bool s_zeroed = false;

static bool s_ok_last = false;
static bool s_back_last = false;
static bool s_left_last = false;
static bool s_right_last = false;

static void fill_traces(void) {
  for (int i = 0; i < WAVE_N; i++) {
    int x = i * WAVE_W / (WAVE_N - 1);
    int yx, yy, yz;
    if (s_zeroed) {
      yx = SCOPE_CENTER_Y;
      yy = SCOPE_CENTER_Y;
      yz = SCOPE_CENTER_Y;
    } else {
      yx = SCOPE_CENTER_Y -
           (AMP_X * lv_trigo_sin((int16_t)((s_phase * SPD_X + i * STEP_X) % 360))) / 32767;
      yy = SCOPE_CENTER_Y -
           (AMP_Y * lv_trigo_sin((int16_t)((s_phase * SPD_Y + i * STEP_Y) % 360))) / 32767;
      yz = Z_BASE_Y -
           (AMP_Z * lv_trigo_sin((int16_t)((s_phase * SPD_Z + i * STEP_Z) % 360))) / 32767;
    }
    s_xpts[i].x = x;
    s_xpts[i].y = yx;
    s_ypts[i].x = x;
    s_ypts[i].y = yy;
    s_zpts[i].x = x;
    s_zpts[i].y = yz;
  }
  if (s_xline)
    lv_line_set_points(s_xline, s_xpts, WAVE_N);
  if (s_yline)
    lv_line_set_points(s_yline, s_ypts, WAVE_N);
  if (s_zline)
    lv_line_set_points(s_zline, s_zpts, WAVE_N);
}

static void refresh_gyro(void) {
  for (int i = 0; i < GYRO_COUNT; i++) {
    int pct = s_zeroed ? 0 : GYRO_ROWS[i].pct;
    int w = pct * GY_TRACK_W / 100;
    int half = GY_TRACK_W / 2;
    int x = GYRO_ROWS[i].right ? half : half - w;
    if (s_gyro_fill[i]) {
      lv_obj_set_width(s_gyro_fill[i], w);
      lv_obj_align(s_gyro_fill[i], LV_ALIGN_LEFT_MID, x, 0);
    }
    if (s_gyro_val[i])
      lv_label_set_text(s_gyro_val[i], s_zeroed ? "+0.00" : GYRO_ROWS[i].val);
  }
}

static lv_obj_t *make_mono_label(lv_obj_t *parent, const char *text, uint32_t color, int x, int y) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x, y);
  return lbl;
}

static lv_obj_t *make_card(int y, int h) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, h);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  return card;
}

static void build_scope_card(void) {
  lv_obj_t *card = make_card(SCOPE_CARD_Y, SCOPE_CARD_H);
  lv_obj_set_style_pad_all(card, SCOPE_PAD, 0);

  lv_obj_t *zero = lv_obj_create(card);
  lv_obj_remove_flag(zero, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(zero, WAVE_W, 1);
  lv_obj_align(zero, LV_ALIGN_TOP_LEFT, 0, SCOPE_CENTER_Y);
  lv_obj_set_style_border_width(zero, 0, 0);
  lv_obj_set_style_radius(zero, 0, 0);
  lv_obj_set_style_bg_color(zero, lv_color_hex(COL_ZERO), 0);
  lv_obj_set_style_bg_opa(zero, LV_OPA_COVER, 0);

  s_zline = lv_line_create(card);
  lv_obj_align(s_zline, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(s_zline, 2, 0);
  lv_obj_set_style_line_color(s_zline, lv_color_hex(COL_WARN), 0);
  lv_obj_set_style_line_rounded(s_zline, true, 0);

  s_yline = lv_line_create(card);
  lv_obj_align(s_yline, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(s_yline, 2, 0);
  lv_obj_set_style_line_color(s_yline, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_line_rounded(s_yline, true, 0);

  s_xline = lv_line_create(card);
  lv_obj_align(s_xline, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(s_xline, 2, 0);
  lv_obj_set_style_line_color(s_xline, current_theme.border_accent, 0);
  lv_obj_set_style_line_rounded(s_xline, true, 0);

  fill_traces();

  lv_obj_t *xval = make_mono_label(card, "X -0.01", COL_DIM, 0, VAL_ROW_Y);
  lv_obj_set_style_text_color(xval, current_theme.border_accent, 0);
  make_mono_label(card, "Y +0.05", COL_CYAN, 80, VAL_ROW_Y);
  make_mono_label(card, "Z +1.00", COL_WARN, 158, VAL_ROW_Y);
}

static void build_gyro_card(void) {
  lv_obj_t *card = make_card(GYRO_CARD_Y, GYRO_CARD_H);
  lv_obj_set_style_pad_all(card, GYRO_PAD, 0);

  for (int i = 0; i < GYRO_COUNT; i++) {
    int row_y = GY_ROW0_Y + i * GY_ROW_STEP;

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, GYRO_ROWS[i].label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, row_y);

    lv_obj_t *track = lv_obj_create(card);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(track, GY_TRACK_W, GY_TRACK_H);
    lv_obj_align(track, LV_ALIGN_TOP_LEFT, GY_TRACK_X, row_y + 4);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_style_bg_color(track, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_border_opa(track, LV_OPA_40, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_clip_corner(track, true, 0);

    lv_obj_t *tick = lv_obj_create(track);
    lv_obj_remove_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tick, 1, GY_TRACK_H);
    lv_obj_align(tick, LV_ALIGN_LEFT_MID, GY_TRACK_W / 2, 0);
    lv_obj_set_style_border_width(tick, 0, 0);
    lv_obj_set_style_radius(tick, 0, 0);
    lv_obj_set_style_bg_color(tick, lv_color_hex(COL_ZERO), 0);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);

    lv_obj_t *fill = lv_obj_create(track);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(fill, 0, GY_FILL_H);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_set_style_bg_color(fill,
                              GYRO_ROWS[i].color ? lv_color_hex(GYRO_ROWS[i].color)
                                                 : current_theme.border_accent,
                              0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    s_gyro_fill[i] = fill;

    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, GYRO_ROWS[i].val);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(val, current_theme.text_main, 0);
    lv_obj_set_width(val, GY_VAL_W);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, GY_VAL_X, row_y);
    s_gyro_val[i] = val;
  }
  refresh_gyro();
}

static void set_rate_label(void) {
  if (!s_rate_lbl)
    return;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "%d Hz", RATES[s_rate_idx]);
  lv_label_set_text(s_rate_lbl, buf);
}

static void scope_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_scope_timer = NULL;
    return;
  }
  if (s_zeroed)
    return;
  s_phase = (s_phase + PHASE_STEP) % 360;
  fill_traces();
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
  bool right = ui_btn_right();

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_MENU);
    return;
  }
  if (ok && !s_ok_last) {
    s_zeroed = !s_zeroed;
    fill_traces();
    refresh_gyro();
    ui_feedback(UI_FB_SELECT);
  }
  if (right && !s_right_last) {
    s_rate_idx = (s_rate_idx + 1) % RATE_COUNT;
    set_rate_label();
    ui_feedback(UI_FB_NAV);
  }

  s_ok_last = ok;
  s_back_last = back;
  s_left_last = left;
  s_right_last = right;
}

void ui_imu_monitor_open(void) {
  if (s_scope_timer != NULL) {
    lv_timer_delete(s_scope_timer);
    s_scope_timer = NULL;
  }
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_xline = s_yline = s_zline = NULL;
  s_rate_lbl = NULL;
  for (int i = 0; i < GYRO_COUNT; i++) {
    s_gyro_fill[i] = NULL;
    s_gyro_val[i] = NULL;
  }
  s_phase = 0;
  s_rate_idx = RATE_COUNT - 1;
  s_zeroed = false;
  s_ok_last = s_back_last = s_left_last = s_right_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *acc_lbl = lv_label_create(s_screen);
  lv_label_set_text(acc_lbl, "ACCEL - SCOPE g");
  lv_obj_set_style_text_font(acc_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(acc_lbl, current_theme.border_accent, 0);
  lv_obj_align(acc_lbl, LV_ALIGN_TOP_LEFT, MX, ACCEL_LBL_Y);

  s_rate_lbl = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_rate_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_rate_lbl, lv_color_hex(COL_CYAN), 0);
  lv_obj_align(s_rate_lbl, LV_ALIGN_TOP_RIGHT, -MX, ACCEL_LBL_Y);
  set_rate_label();

  build_scope_card();

  lv_obj_t *gyro_lbl = lv_label_create(s_screen);
  lv_label_set_text(gyro_lbl, "GYRO - dps");
  lv_obj_set_style_text_font(gyro_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(gyro_lbl, lv_color_hex(COL_CYAN), 0);
  lv_obj_align(gyro_lbl, LV_ALIGN_TOP_LEFT, MX, GYRO_LBL_Y);

  build_gyro_card();

  ui_chrome_footer(s_screen, FOOTER_TXT);

  s_scope_timer = lv_timer_create(scope_tick_cb, SCOPE_TICK_MS, NULL);
  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

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

#include "subghz_scope_ui.h"

#include "ui_theme.h"

#define SCOPE_W       208
#define SCOPE_H       84
#define SCOPE_PAD     6
#define SCOPE_RADIUS  8
#define SCOPE_BORDER  2
#define SCOPE_BG      0x0A0614
#define SCOPE_TICK_MS 38

#define WAVE_POINTS 49
#define WAVE_W      (SCOPE_W - SCOPE_PAD * 2 - SCOPE_BORDER * 2)
#define WAVE_H      (SCOPE_H - SCOPE_PAD * 2 - SCOPE_BORDER * 2)
#define WAVE_CY     (WAVE_H / 2)
#define WAVE_LINE_W 2

#define AMP_SCAN        (WAVE_H / 2 - 4)
#define AMP_VAR         (WAVE_H / 6)
#define AMP_LOCK        (WAVE_H / 3)
#define ANGLE_STEP_BASE 15
#define ANGLE_VAR       9
#define ANGLE_STEP_LOCK 15
#define PHASE_STEP_SCAN 34
#define MOD_STEP        6
#define NOISE_SPREAD    7

#define OOK_SYNC_T    2
#define OOK_HI_WIDE   3
#define OOK_HI_NARROW 1
#define OOK_MAX_PTS   64

#define GRID_OPA LV_OPA_20

static const uint8_t OOK_BITS[] = {0, 0, 0, 1, 1, 0};
#define OOK_BIT_COUNT ((int)(sizeof(OOK_BITS) / sizeof(OOK_BITS[0])))

static lv_obj_t *s_wave = NULL;
static lv_timer_t *s_timer = NULL;
static lv_point_precise_t s_wave_pts[WAVE_POINTS];
static lv_point_precise_t s_ook_pts[OOK_MAX_PTS];
static int s_phase = 0;
static int s_mod = 0;
static bool s_locked = false;

static int clamp_y(int y) {
  if (y < 0)
    return 0;
  if (y > WAVE_H)
    return WAVE_H;
  return y;
}

static void fill_wave(void) {
  int step = ANGLE_STEP_BASE + (ANGLE_VAR * lv_trigo_sin((int16_t)(s_mod % 360))) / 32767;
  int amp = AMP_SCAN - (AMP_VAR * lv_trigo_sin((int16_t)((s_mod * 2) % 360))) / 32767;
  for (int i = 0; i < WAVE_POINTS; i++) {
    int ang = (s_phase + i * step) % 360;
    if (ang < 0)
      ang += 360;
    int s = lv_trigo_sin((int16_t)ang);
    int y = WAVE_CY - (amp * s) / 32767;
    y += ((i * 13 + s_phase) % NOISE_SPREAD) - NOISE_SPREAD / 2;
    s_wave_pts[i].x = i * WAVE_W / (WAVE_POINTS - 1);
    s_wave_pts[i].y = clamp_y(y);
  }
  if (s_wave != NULL)
    lv_line_set_points(s_wave, s_wave_pts, WAVE_POINTS);
}

static void fill_ook(void) {
  if (s_wave == NULL)
    return;
  int total_t = OOK_SYNC_T + OOK_BIT_COUNT * (OOK_HI_WIDE + OOK_HI_NARROW);
  int unit = WAVE_W / total_t;
  if (unit < 1)
    unit = 1;
  int hi = WAVE_CY - AMP_LOCK;
  int lo = WAVE_CY + AMP_LOCK;
  int n = 0;
  int x = 0;
  s_ook_pts[n].x = x;
  s_ook_pts[n].y = lo;
  n++;
  x += OOK_SYNC_T * unit;
  s_ook_pts[n].x = x;
  s_ook_pts[n].y = lo;
  n++;
  for (int b = 0; b < OOK_BIT_COUNT && n + 4 <= OOK_MAX_PTS; b++) {
    int hw = (OOK_BITS[b] ? OOK_HI_WIDE : OOK_HI_NARROW) * unit;
    int lw = (OOK_BITS[b] ? OOK_HI_NARROW : OOK_HI_WIDE) * unit;
    s_ook_pts[n].x = x;
    s_ook_pts[n].y = hi;
    n++;
    x += hw;
    s_ook_pts[n].x = x;
    s_ook_pts[n].y = hi;
    n++;
    s_ook_pts[n].x = x;
    s_ook_pts[n].y = lo;
    n++;
    x += lw;
    s_ook_pts[n].x = x;
    s_ook_pts[n].y = lo;
    n++;
  }
  lv_line_set_points(s_wave, s_ook_pts, n);
}

static void scope_tick_cb(lv_timer_t *t) {
  if (!lv_obj_is_valid(s_wave)) {
    lv_timer_delete(t);
    s_timer = NULL;
    s_wave = NULL;
    return;
  }
  if (s_locked)
    return;
  s_phase = (s_phase + PHASE_STEP_SCAN) % 360;
  s_mod = (s_mod + MOD_STEP) % 360;
  fill_wave();
}

void subghz_scope_stop(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
}

void subghz_scope_lock(void) {
  s_locked = true;
  if (lv_obj_is_valid(s_wave)) {
    lv_obj_set_style_line_rounded(s_wave, false, 0);
    fill_ook();
  }
}

lv_obj_t *subghz_scope_create(lv_obj_t *parent, lv_align_t align, int32_t x_ofs, int32_t y_ofs) {
  subghz_scope_stop();
  s_locked = false;
  s_phase = 0;
  s_mod = 0;

  lv_obj_t *frame = lv_obj_create(parent);
  lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(frame, SCOPE_W, SCOPE_H);
  lv_obj_align(frame, align, x_ofs, y_ofs);
  lv_obj_set_style_radius(frame, SCOPE_RADIUS, 0);
  lv_obj_set_style_pad_all(frame, SCOPE_PAD, 0);
  lv_obj_set_style_bg_color(frame, lv_color_hex(SCOPE_BG), 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(frame, SCOPE_BORDER, 0);
  lv_obj_set_style_border_color(frame, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(frame, LV_OPA_70, 0);

  lv_obj_t *grid = lv_obj_create(frame);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grid, WAVE_W, 1);
  lv_obj_align(grid, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_radius(grid, 0, 0);
  lv_obj_set_style_bg_color(grid, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(grid, GRID_OPA, 0);

  s_wave = lv_line_create(frame);
  lv_obj_align(s_wave, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(s_wave, WAVE_LINE_W, 0);
  lv_obj_set_style_line_color(s_wave, current_theme.border_accent, 0);
  lv_obj_set_style_line_rounded(s_wave, true, 0);

  fill_wave();
  s_timer = lv_timer_create(scope_tick_cb, SCOPE_TICK_MS, NULL);
  return frame;
}

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

#include "subghz_brute_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "notify_ui.h"
#include "subghz_brute.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

static const char *TAG = "SUBGHZ_BF";

#define TICK_MS       120
#define SCOPE_TICK_MS 38
#define DOT_CYCLE_MS  350

#define STATUS_Y 48
#define FREQ_Y   68

#define SCOPE_W      208
#define SCOPE_H      84
#define SCOPE_Y_OFS  -22
#define SCOPE_PAD    6
#define SCOPE_RADIUS 8
#define SCOPE_BORDER 2
#define SCOPE_BG     0x0A0614

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

#define BAR_W     192
#define BAR_H     8
#define BAR_Y     (CARD_Y + 20)
#define CODES_Y   (CARD_Y + 46)
#define TRACK_COL 0x202028

#define CARD_W             210
#define CARD_H             96
#define CARD_Y             LV_MIN(190, ui_screen_h() - UI_CHROME_FOOTER_H - CARD_H)
#define CARD_RADIUS        12
#define CARD_SHADOW_W      14
#define CARD_SHADOW_SPREAD -3

#define STATUS_RUN "Transmitting"
#define HINT_RUN   "BACK to stop"
#define HINT_SHOW  "BACK = Exit"

#define BRUTE_PROTO_NAME "CAME"
#define BRUTE_BITS       12
#define BRUTE_FREQ_HZ    433920000
#define BRUTE_TOTAL      (1 << BRUTE_BITS)
#define BRUTE_FREQ_STR   "433.92 MHz"

static const uint8_t OOK_BITS[] = {0, 0, 0, 1, 1, 0};
#define OOK_BIT_COUNT ((int)(sizeof(OOK_BITS) / sizeof(OOK_BITS[0])))

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_tick_timer = NULL;
static lv_timer_t *s_scope_timer = NULL;

static lv_obj_t *s_status = NULL;
static lv_obj_t *s_freq = NULL;
static lv_obj_t *s_scope = NULL;
static lv_obj_t *s_wave = NULL;
static lv_obj_t *s_card = NULL;
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_codes = NULL;
static lv_obj_t *s_hint = NULL;

static lv_point_precise_t s_wave_pts[WAVE_POINTS];
static lv_point_precise_t s_ook_pts[OOK_MAX_PTS];
static int s_phase = 0;
static int s_mod = 0;
static uint32_t s_run_start = 0;
static bool s_locked = false;

static void brute_tick_cb(lv_timer_t *t);
static void scope_tick_cb(lv_timer_t *t);
static void subghz_brute_input(const input_event_t *ev, void *ctx);

static void stop_timer(lv_timer_t **t) {
  if (*t != NULL) {
    lv_timer_delete(*t);
    *t = NULL;
  }
}

static int clamp_y(int y) {
  if (y < 0)
    return 0;
  if (y > WAVE_H)
    return WAVE_H;
  return y;
}

static void fill_wave(bool noisy) {
  int step = ANGLE_STEP_LOCK;
  int amp = AMP_LOCK;
  if (noisy) {
    step = ANGLE_STEP_BASE + (ANGLE_VAR * lv_trigo_sin((int16_t)(s_mod % 360))) / 32767;
    amp = AMP_SCAN - (AMP_VAR * lv_trigo_sin((int16_t)((s_mod * 2) % 360))) / 32767;
  }
  for (int i = 0; i < WAVE_POINTS; i++) {
    int ang = (s_phase + i * step) % 360;
    if (ang < 0)
      ang += 360;
    int s = lv_trigo_sin((int16_t)ang);
    int y = WAVE_CY - (amp * s) / 32767;
    if (noisy)
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

static void build_scope(void) {
  lv_obj_t *frame = lv_obj_create(s_screen);
  s_scope = frame;
  lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(frame, SCOPE_W, SCOPE_H);
  lv_obj_align(frame, LV_ALIGN_CENTER, 0, SCOPE_Y_OFS);
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

  s_phase = 0;
  s_mod = 0;
  fill_wave(true);
}

static void build_readout(void) {
  s_card = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_card, CARD_W, CARD_H);
  lv_obj_align(s_card, LV_ALIGN_TOP_MID, 0, CARD_Y);
  lv_obj_set_style_radius(s_card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(s_card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(s_card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(s_card, 1, 0);
  lv_obj_set_style_border_color(s_card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(s_card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(s_card, CARD_SHADOW_W, 0);
  lv_obj_set_style_shadow_opa(s_card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(s_card, CARD_SHADOW_SPREAD, 0);
  lv_obj_set_style_pad_all(s_card, 0, 0);

  s_bar = lv_bar_create(s_screen);
  lv_obj_set_size(s_bar, BAR_W, BAR_H);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, BAR_Y);
  lv_obj_set_style_radius(s_bar, 4, 0);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(TRACK_COL), 0);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(s_bar, 0, BRUTE_TOTAL);
  lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

  s_codes = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_codes, "Sent  0 / %d", BRUTE_TOTAL);
  lv_obj_set_style_text_color(s_codes, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_codes, &lv_font_montserrat_12, 0);
  lv_obj_align(s_codes, LV_ALIGN_TOP_MID, 0, CODES_Y);
}

static void finish_sweep(void) {
  s_locked = true;
  stop_timer(&s_scope_timer);
  if (s_wave != NULL)
    lv_obj_set_style_line_rounded(s_wave, false, 0);
  fill_ook();

  if (s_bar != NULL)
    lv_bar_set_value(s_bar, BRUTE_TOTAL, LV_ANIM_OFF);
  if (s_codes != NULL)
    lv_label_set_text_fmt(s_codes, "Sent  %d / %d", BRUTE_TOTAL, BRUTE_TOTAL);
  if (s_status != NULL) {
    lv_label_set_text(s_status, "Sweep complete");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_SUCCESS), 0);
  }
  if (s_hint != NULL)
    ui_chrome_footer_set_text(s_hint, HINT_SHOW);

  ESP_LOGI(TAG, "brute sweep complete (%d codes)", BRUTE_TOTAL);
  ui_feedback(UI_FB_WRITE);
}

void ui_subghz_brute_open(void) {
  subghz_brute_stop();
  stop_timer(&s_scope_timer);
  stop_timer(&s_tick_timer);
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status = NULL;
  s_freq = NULL;
  s_scope = NULL;
  s_wave = NULL;
  s_card = NULL;
  s_bar = NULL;
  s_codes = NULL;
  s_hint = NULL;
  s_phase = 0;
  s_mod = 0;
  s_locked = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "BRUTE FORCE", "/assets/icons/bolt.bin");

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, STATUS_RUN);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_freq = lv_label_create(s_screen);
  lv_label_set_text(s_freq, BRUTE_PROTO_NAME "  " BRUTE_FREQ_STR);
  lv_obj_set_style_text_color(s_freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_freq, &lv_font_montserrat_12, 0);
  lv_obj_align(s_freq, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  build_scope();
  build_readout();

  s_hint = ui_chrome_footer(s_screen, HINT_RUN);

  s_run_start = lv_tick_get();
  s_scope_timer = lv_timer_create(scope_tick_cb, SCOPE_TICK_MS, NULL);
  s_tick_timer = lv_timer_create(brute_tick_cb, TICK_MS, NULL);

  ui_input_set_screen_handler(subghz_brute_input, NULL);

  esp_err_t berr = subghz_brute_start(BRUTE_PROTO_NAME, BRUTE_BITS, BRUTE_FREQ_HZ);
  if (berr != ESP_OK) {
    s_locked = true;
    stop_timer(&s_scope_timer);
    if (s_status != NULL) {
      lv_label_set_text(s_status, "Unavailable");
      lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);
    }
    if (s_hint != NULL)
      ui_chrome_footer_set_text(s_hint, HINT_SHOW);
    notify(NOTIFY_WARNING, "Brute unavailable");
  }

  ui_screen_load_owned(&s_screen, s_screen);
}

static void scope_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_scope_timer = NULL;
    return;
  }
  s_phase = (s_phase + PHASE_STEP_SCAN) % 360;
  s_mod = (s_mod + MOD_STEP) % 360;
  fill_wave(true);
}

static void brute_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_tick_timer = NULL;
    return;
  }
  if (s_locked)
    return;

  if (s_status != NULL) {
    int dots = ((lv_tick_get() - s_run_start) / DOT_CYCLE_MS) % 4;
    char buf[24];
    snprintf(buf,
             sizeof(buf),
             "%s%s",
             STATUS_RUN,
             dots == 1   ? "."
             : dots == 2 ? ".."
             : dots == 3 ? "..."
                         : "");
    lv_label_set_text(s_status, buf);
  }

  subghz_brute_status_t st;
  if (!subghz_brute_get_status(&st))
    return;

  int total = st.total ? (int)st.total : BRUTE_TOTAL;
  int sent = (int)st.sent;
  if (s_bar != NULL) {
    lv_bar_set_range(s_bar, 0, total);
    lv_bar_set_value(s_bar, sent, LV_ANIM_OFF);
  }
  if (s_codes != NULL)
    lv_label_set_text_fmt(s_codes, "Sent  %d / %d", sent, total);

  if (st.done)
    finish_sweep();
}

static void subghz_brute_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  if (ev->button == INPUT_BTN_BACK) {
    if (press) {
      subghz_brute_stop();
      ui_switch_screen(SCREEN_SUBGHZ_MENU);
    }
    return;
  }
}

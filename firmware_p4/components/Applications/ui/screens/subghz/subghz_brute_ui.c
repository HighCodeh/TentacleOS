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

#include "buttons_gpio.h"
#include "capture_result_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "SUBGHZ_BF";

#define NAV_TIMER_MS  33
#define REVEAL_MS     3000
#define BRUTE_MS      4200
#define SCOPE_TICK_MS 38
#define DOT_CYCLE_MS  350

#define SIG_GREEN 0x00E676

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
#define BAR_Y     210
#define CODES_Y   234
#define HIT_Y     258
#define TRACK_COL 0x202028

#define CARD_W             210
#define CARD_H             96
#define CARD_Y             190
#define CARD_RADIUS        12
#define CARD_SHADOW_W      14
#define CARD_SHADOW_SPREAD -3

#define STATUS_RUN "Transmitting"
#define HINT_RUN   "BACK to stop"
#define HINT_SHOW  "BACK = Exit"
#define HINT_MENU  "UP/DOWN choose   OK do   BACK exit"

#define BRUTE_PROTO "Princeton"
#define BRUTE_FREQ  "433.92 MHz"
#define BRUTE_HIT   "0x1A2B3C"
#define BRUTE_TOTAL 4096
#define BRUTE_HIT_N (BRUTE_TOTAL / 3)

static const uint8_t OOK_BITS[] = {0, 0, 0, 1, 1, 0};
#define OOK_BIT_COUNT ((int)(sizeof(OOK_BITS) / sizeof(OOK_BITS[0])))

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_scope_timer = NULL;

static lv_obj_t *s_status = NULL;
static lv_obj_t *s_freq = NULL;
static lv_obj_t *s_scope = NULL;
static lv_obj_t *s_wave = NULL;
static lv_obj_t *s_card = NULL;
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_codes = NULL;
static lv_obj_t *s_hit = NULL;
static lv_obj_t *s_hint = NULL;

static capture_result_t s_cr = {0};
static lv_point_precise_t s_wave_pts[WAVE_POINTS];
static lv_point_precise_t s_ook_pts[OOK_MAX_PTS];
static int s_phase = 0;
static int s_mod = 0;
static int s_code_count = 0;
static uint32_t s_run_start = 0;
static uint32_t s_locked_at = 0;
static bool s_locked = false;
static bool s_options = false;
static bool s_saved = false;

static bool s_right_last, s_ok_last, s_back_last, s_up_last, s_down_last;

static void nav_timer_cb(lv_timer_t *t);
static void scope_tick_cb(lv_timer_t *t);

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
  lv_label_set_text_fmt(s_codes, "Codes  0 / %d", BRUTE_TOTAL);
  lv_obj_set_style_text_color(s_codes, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_codes, &lv_font_montserrat_12, 0);
  lv_obj_align(s_codes, LV_ALIGN_TOP_MID, 0, CODES_Y);

  s_hit = lv_label_create(s_screen);
  lv_label_set_text(s_hit, BRUTE_PROTO "  " BRUTE_HIT);
  lv_obj_set_style_text_color(s_hit, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_hit, &lv_font_montserrat_12, 0);
  lv_obj_align(s_hit, LV_ALIGN_TOP_MID, 0, HIT_Y);
  lv_obj_add_flag(s_hit, LV_OBJ_FLAG_HIDDEN);
}

static void resolve(void) {
  s_locked = true;
  stop_timer(&s_scope_timer);
  if (s_wave != NULL)
    lv_obj_set_style_line_rounded(s_wave, false, 0);
  fill_ook();

  s_code_count = BRUTE_HIT_N;
  if (s_bar != NULL)
    lv_bar_set_value(s_bar, s_code_count, LV_ANIM_OFF);
  if (s_codes != NULL)
    lv_label_set_text_fmt(s_codes, "Hit at code %d", s_code_count);
  if (s_hit != NULL)
    lv_obj_remove_flag(s_hit, LV_OBJ_FLAG_HIDDEN);
  if (s_status != NULL) {
    lv_label_set_text(s_status, "Code found!");
    lv_obj_set_style_text_color(s_status, lv_color_hex(SIG_GREEN), 0);
  }
  if (s_hint != NULL)
    ui_chrome_footer_set_text(s_hint, HINT_SHOW);
  s_locked_at = lv_tick_get();

  ESP_LOGI(TAG, "mock subghz brute hit: %s %s", BRUTE_PROTO, BRUTE_HIT);
  ui_feedback(UI_FB_WRITE);
}

static void show_options(void) {
  if (s_scope != NULL) {
    lv_obj_del(s_scope);
    s_scope = NULL;
    s_wave = NULL;
  }
  if (s_bar != NULL) {
    lv_obj_del(s_bar);
    s_bar = NULL;
  }
  if (s_codes != NULL) {
    lv_obj_del(s_codes);
    s_codes = NULL;
  }
  if (s_hit != NULL) {
    lv_obj_del(s_hit);
    s_hit = NULL;
  }
  if (s_card != NULL) {
    lv_obj_del(s_card);
    s_card = NULL;
  }
  if (s_status != NULL)
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  if (s_freq != NULL)
    lv_obj_add_flag(s_freq, LV_OBJ_FLAG_HIDDEN);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = "/assets/icons/bolt.bin",
      .card_title = "Code found",
      .card_sub = BRUTE_PROTO " (OOK)",
      .card_value = BRUTE_HIT,
      .primary_label = "Send",
      .again_label = "Run again",
  };
  s_cr = capture_result_create(s_screen, &cfg);
  s_options = true;
  if (s_hint != NULL)
    ui_chrome_footer_set_text(s_hint, HINT_MENU);
}

void ui_subghz_brute_open(void) {
  stop_timer(&s_scope_timer);
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
  s_hit = NULL;
  s_hint = NULL;
  s_cr = (capture_result_t){0};
  s_phase = 0;
  s_mod = 0;
  s_code_count = 0;
  s_locked = false;
  s_options = false;
  s_saved = false;
  s_locked_at = 0;
  s_back_last = s_ok_last = s_right_last = s_up_last = s_down_last = false;

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
  lv_label_set_text(s_freq, BRUTE_FREQ);
  lv_obj_set_style_text_color(s_freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_freq, &lv_font_montserrat_12, 0);
  lv_obj_align(s_freq, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  build_scope();
  build_readout();

  s_hint = ui_chrome_footer(s_screen, HINT_RUN);

  s_run_start = lv_tick_get();
  s_scope_timer = lv_timer_create(scope_tick_cb, SCOPE_TICK_MS, NULL);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
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

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();
  bool up = ui_btn_up();
  bool down = ui_btn_down();

  if (msgbox_is_open() || ui_input_is_locked()) {
    s_right_last = right;
    s_ok_last = ok;
    s_back_last = back;
    s_up_last = up;
    s_down_last = down;
    return;
  }

  if (!s_locked) {
    uint32_t el = lv_tick_get() - s_run_start;
    if (s_status != NULL) {
      int dots = (el / DOT_CYCLE_MS) % 4;
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
    int count = (int)((uint64_t)el * BRUTE_TOTAL / BRUTE_MS);
    if (count > BRUTE_TOTAL)
      count = BRUTE_TOTAL;
    s_code_count = count;
    if (s_bar != NULL)
      lv_bar_set_value(s_bar, count, LV_ANIM_OFF);
    if (s_codes != NULL)
      lv_label_set_text_fmt(s_codes, "Codes  %d / %d", count, BRUTE_TOTAL);
  }

  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_SUBGHZ_MENU);
    return;
  }

  if (!s_locked) {
    if (lv_tick_get() - s_run_start >= BRUTE_MS)
      resolve();
  } else if (!s_options) {
    if (lv_tick_get() - s_locked_at >= REVEAL_MS)
      show_options();
  } else {
    if (down && !s_down_last) {
      capture_result_next(&s_cr);
      ui_feedback(UI_FB_NAV);
    }
    if (up && !s_up_last) {
      capture_result_prev(&s_cr);
      ui_feedback(UI_FB_NAV);
    }
    if (ok && !s_ok_last) {
      switch (capture_result_selected(&s_cr)) {
        case CAP_ACT_PRIMARY:
          ui_feedback(UI_FB_EMULATE);
          notify(NOTIFY_INFO, BRUTE_FREQ " sent");
          break;
        case CAP_ACT_SAVE:
          if (!s_saved) {
            s_saved = true;
            capture_result_mark_saved(&s_cr);
            ESP_LOGI(TAG, "mock subghz brute saved: %s", BRUTE_HIT);
            ui_feedback(UI_FB_WRITE);
            notify(NOTIFY_SAVED, "Sub-GHz code saved");
          }
          break;
        case CAP_ACT_AGAIN:
          ui_subghz_brute_open();
          return;
        case CAP_ACT_DISCARD:
          ui_switch_screen(SCREEN_SUBGHZ_MENU);
          return;
        default:
          break;
      }
    }
  }

  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
  s_up_last = up;
  s_down_last = down;
}

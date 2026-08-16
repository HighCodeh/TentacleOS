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

#include "subghz_menu_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "capture_result_ui.h"
#include "cc1101.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "subghz_receiver.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "SUBGHZ_RD";

#define RX_PRESET CC1101_PRESET_OOK_800KHZ
#define RX_FREQ_HOPPING 0

#define TICK_MS       33
#define REVEAL_MS     3000
#define SCAN_MS       2600
#define FREQ_CYCLE_MS 420
#define SCOPE_TICK_MS 38
#define DOT_CYCLE_MS  350

#define SIG_GREEN 0x00E676

#define HEADER_TITLE_Y 10
#define HEADER_RULE_Y  32
#define HEADER_RULE_W  70
#define HEADER_RULE_H  2

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

#define READOUT_W       192
#define READOUT_Y       198
#define READOUT_ROW_GAP 5
#define READOUT_FADE_MS 240
#define READOUT_STAGGER 70

#define HINT_Y_OFS -6

#define STATUS_SCAN "Scanning"
#define HINT_SCAN   "BACK to cancel"
#define HINT_SHOW   "BACK = Exit"
#define HINT_MENU   "UP/DOWN choose   OK do   BACK exit"

#define SIG_PROTO     "Princeton"
#define SIG_LOCK_FREQ "433.92 MHz"

static const char *SCAN_FREQS[] = {
    "433.92 MHz",
    "868.30 MHz",
    "315.00 MHz",
    "915.00 MHz",
};
#define SCAN_FREQ_COUNT ((int)(sizeof(SCAN_FREQS) / sizeof(SCAN_FREQS[0])))

static const struct {
  const char *label;
  const char *value;
} SIG_ROWS[] = {
    {"Protocol", SIG_PROTO},
    {"Modulation", "OOK"},
    {"Bitrate", "4.8 kb/s"},
    {"Key", "0x1A2B3C"},
};
#define SIG_ROW_COUNT ((int)(sizeof(SIG_ROWS) / sizeof(SIG_ROWS[0])))

static const uint8_t OOK_BITS[] = {0, 0, 0, 1, 1, 0};
#define OOK_BIT_COUNT ((int)(sizeof(OOK_BITS) / sizeof(OOK_BITS[0])))

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_tick_timer = NULL;
static lv_timer_t *s_scan_timer = NULL;
static lv_timer_t *s_freq_timer = NULL;
static lv_timer_t *s_scope_timer = NULL;

static lv_obj_t *s_status = NULL;
static lv_obj_t *s_freq = NULL;
static lv_obj_t *s_wave = NULL;
static lv_obj_t *s_scope = NULL;
static lv_obj_t *s_readout = NULL;
static lv_obj_t *s_hint = NULL;
static capture_result_t s_cr = {0};
static uint32_t s_locked_at = 0;
static bool s_options = false;

static lv_point_precise_t s_wave_pts[WAVE_POINTS];
static lv_point_precise_t s_ook_pts[OOK_MAX_PTS];
static int s_phase = 0;
static int s_mod = 0;
static int s_freq_idx = 0;
static uint32_t s_scan_start = 0;
static bool s_locked = false;
static bool s_saved = false;

static void read_tick_cb(lv_timer_t *t);
static void scan_done_cb(lv_timer_t *t);
static void freq_cycle_cb(lv_timer_t *t);
static void scope_tick_cb(lv_timer_t *t);
static void subghz_read_input(const input_event_t *ev, void *ctx);

static void stop_timer(lv_timer_t **t) {
  if (*t != NULL) {
    lv_timer_delete(*t);
    *t = NULL;
  }
}

static void stop_rx(void) {
  if (subghz_receiver_is_running())
    subghz_receiver_stop();
}

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fade_in(lv_obj_t *obj, uint32_t duration_ms, uint32_t delay_ms) {
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, duration_ms);
  lv_anim_set_delay(&a, delay_ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void build_header(const char *text) {
  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, text);
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, HEADER_TITLE_Y);

  lv_obj_t *rule = lv_obj_create(s_screen);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(rule, lv_pct(HEADER_RULE_W), HEADER_RULE_H);
  lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, HEADER_RULE_Y);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, 1, 0);
  lv_obj_set_style_bg_color(rule, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);
}

static lv_obj_t *make_hint(const char *text) {
  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, text);
  lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(hint, LV_OPA_60, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, HINT_Y_OFS);
  return hint;
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

void ui_subghz_read_open(void) {
  stop_rx();
  stop_timer(&s_scan_timer);
  stop_timer(&s_freq_timer);
  stop_timer(&s_scope_timer);
  stop_timer(&s_tick_timer);
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status = NULL;
  s_freq = NULL;
  s_wave = NULL;
  s_scope = NULL;
  s_readout = NULL;
  s_hint = NULL;
  s_cr = (capture_result_t){0};
  s_options = false;
  s_locked_at = 0;
  s_freq_idx = 0;
  s_locked = false;
  s_saved = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "READ", "/assets/icons/sensors.bin");

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, STATUS_SCAN);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_freq = lv_label_create(s_screen);
  lv_label_set_text(s_freq, SCAN_FREQS[0]);
  lv_obj_set_style_text_color(s_freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_freq, &lv_font_montserrat_12, 0);
  lv_obj_align(s_freq, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  build_scope();

  s_hint = ui_chrome_footer(s_screen, HINT_SCAN);

  s_scan_start = lv_tick_get();
  s_scan_timer = lv_timer_create(scan_done_cb, SCAN_MS, NULL);
  lv_timer_set_repeat_count(s_scan_timer, 1);
  s_freq_timer = lv_timer_create(freq_cycle_cb, FREQ_CYCLE_MS, NULL);
  s_scope_timer = lv_timer_create(scope_tick_cb, SCOPE_TICK_MS, NULL);
  s_tick_timer = lv_timer_create(read_tick_cb, TICK_MS, NULL);

  ui_input_set_screen_handler(subghz_read_input, NULL);

  esp_err_t rx = subghz_receiver_start(SUBGHZ_MODE_SCAN, RX_PRESET, RX_FREQ_HOPPING);
  if (rx != ESP_OK)
    ESP_LOGE(TAG, "subghz_receiver_start failed: %s", esp_err_to_name(rx));

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

static void freq_cycle_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_freq_timer = NULL;
    return;
  }
  s_freq_idx = (s_freq_idx + 1) % SCAN_FREQ_COUNT;
  if (s_freq)
    lv_label_set_text(s_freq, SCAN_FREQS[s_freq_idx]);
}

static void build_signal_readout(void) {
  lv_obj_t *col = lv_obj_create(s_screen);
  s_readout = col;
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(col, READOUT_W);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, READOUT_Y);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, READOUT_ROW_GAP, 0);

  for (int i = 0; i < SIG_ROW_COUNT; i++) {
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, SIG_ROWS[i].label);
    lv_obj_set_style_text_color(label, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);

    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, SIG_ROWS[i].value);
    lv_obj_set_style_text_color(value, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);

    fade_in(row, READOUT_FADE_MS, i * READOUT_STAGGER);
  }
}

static void scan_done_cb(lv_timer_t *t) {
  (void)t;
  s_scan_timer = NULL;
  stop_timer(&s_freq_timer);
  stop_timer(&s_scope_timer);
  stop_rx();
  if (lv_screen_active() != s_screen)
    return;

  s_locked = true;
  if (s_wave != NULL)
    lv_obj_set_style_line_rounded(s_wave, false, 0);
  fill_ook();

  if (s_status) {
    lv_label_set_text(s_status, "Signal locked!");
    lv_obj_set_style_text_color(s_status, lv_color_hex(SIG_GREEN), 0);
  }
  if (s_freq)
    lv_label_set_text(s_freq, SIG_LOCK_FREQ);

  build_signal_readout();

  if (s_hint != NULL)
    ui_chrome_footer_set_text(s_hint, HINT_SHOW);
  s_locked_at = lv_tick_get();

  ESP_LOGI(TAG, "mock subghz capture: %s %s", SIG_PROTO, SIG_LOCK_FREQ);
  ui_feedback(UI_FB_READ);
}

static void show_options(void) {
  if (s_scope) {
    lv_obj_del(s_scope);
    s_scope = NULL;
    s_wave = NULL;
  }
  if (s_readout) {
    lv_obj_del(s_readout);
    s_readout = NULL;
  }
  if (s_status)
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  if (s_freq)
    lv_obj_add_flag(s_freq, LV_OBJ_FLAG_HIDDEN);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = "/assets/icons/graphic_eq.bin",
      .card_title = "Signal captured",
      .card_sub = SIG_PROTO " (OOK)",
      .card_value = SIG_LOCK_FREQ,
      .primary_label = "Send",
      .again_label = "Capture again",
  };
  s_cr = capture_result_create(s_screen, &cfg);
  s_options = true;
  if (s_hint)
    ui_chrome_footer_set_text(s_hint, HINT_MENU);
}

static void read_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_tick_timer = NULL;
    return;
  }

  if (!s_locked && s_status != NULL) {
    int dots = ((lv_tick_get() - s_scan_start) / DOT_CYCLE_MS) % 4;
    char buf[20];
    snprintf(buf,
             sizeof(buf),
             "%s%s",
             STATUS_SCAN,
             dots == 1   ? "."
             : dots == 2 ? ".."
             : dots == 3 ? "..."
                         : "");
    lv_label_set_text(s_status, buf);
  }

  if (s_locked && !s_options) {
    if (lv_tick_get() - s_locked_at >= REVEAL_MS)
      show_options();
  }
}

static void subghz_read_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (ev->button == INPUT_BTN_BACK) {
    if (press) {
      stop_rx();
      ui_switch_screen(SCREEN_SUBGHZ_MENU);
    }
    return;
  }

  if (!s_options)
    return;

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) {
        capture_result_next(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        capture_result_prev(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        switch (capture_result_selected(&s_cr)) {
          case CAP_ACT_PRIMARY:
            ui_feedback(UI_FB_EMULATE);
            notify(NOTIFY_INFO, SIG_LOCK_FREQ " sent");
            break;
          case CAP_ACT_SAVE:
            if (!s_saved) {
              s_saved = true;
              capture_result_mark_saved(&s_cr);
              ESP_LOGI(TAG, "mock subghz saved: %s", SIG_PROTO);
              ui_feedback(UI_FB_WRITE);
              notify(NOTIFY_SAVED, "Sub-GHz signal saved");
            }
            break;
          case CAP_ACT_AGAIN:
            ui_subghz_read_open();
            return;
          case CAP_ACT_DISCARD:
            stop_rx();
            ui_switch_screen(SCREEN_SUBGHZ_MENU);
            return;
          default:
            break;
        }
      }
      break;
    default:
      break;
  }
}

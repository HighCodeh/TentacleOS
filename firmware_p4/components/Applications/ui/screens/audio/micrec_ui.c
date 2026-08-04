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

#include "micrec_ui.h"

#include <math.h>
#include <string.h>

#include "audio_i2s.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "MICREC_UI";

#define NAV_TIMER_MS       50
#define REC_RATE           16000
#define REC_MAX_SECONDS    5
#define BUF_HEADROOM       (24 * 1024)
#define ACCENT_GREEN       0x00E676
#define NORM_MAX_GAIN_Q8   (64 * 256)
#define VU_FULLSCALE_PEAK  7000
#define SCOPE_W            80
#define SCOPE_FULL         9000.0f
#define OVERLAY_TICK_MS    60
#define OVERLAY_HIDE_MS    700
#define MIC_TASK_STACK     4096
#define MIC_TASK_PRIORITY  4
#define REC_TARGET_DEFAULT 26000
#define REC_TARGET_MIN     16000
#define REC_TARGET_STEP    3400

enum { ROW_LEVEL, ROW_REC, ROW_PLAY, ROW_LOOP, ROW_COUNT };
enum { OV_TIME, OV_VU };
enum { ST_IDLE, ST_RECORDING, ST_PLAYING };

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static lv_obj_t *s_status_lbl = NULL;

static lv_obj_t *s_ov = NULL;
static lv_obj_t *s_ov_label = NULL;
static lv_obj_t *s_ov_bar = NULL;
static lv_obj_t *s_scope_line = NULL;
static lv_timer_t *s_ov_timer = NULL;
static int s_ov_mode = OV_TIME;
static uint32_t s_ov_start = 0;
static uint32_t s_ov_total = 1;
static volatile bool s_op_done = false;
static char s_done_text[24] = "";
static volatile int s_live_peak = 0;
static volatile int s_live_rms = 0;
static int s_vu_display = 0;

static volatile int16_t s_scope[SCOPE_W];
static volatile int s_scope_head = 0;
static lv_point_precise_t s_scope_pts[SCOPE_W];

static int16_t *s_rec_buf = NULL;
static size_t s_rec_capacity = 0;
static volatile size_t s_rec_samples = 0;
static volatile bool s_busy = false;
static volatile bool s_stop_req = false;
static volatile int s_state = ST_IDLE;
static bool s_loop = false;
static uint32_t s_last_ms = 0;
static int s_rec_target = REC_TARGET_DEFAULT;

static bool s_btn_up_last, s_btn_down_last, s_btn_left_last, s_btn_right_last;
static bool s_btn_ok_last, s_btn_back_last;

static bool ensure_buffer(void) {
  if (s_rec_buf != NULL)
    return true;
  size_t want = (size_t)REC_RATE * REC_MAX_SECONDS;
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  size_t avail = (largest > BUF_HEADROOM) ? (largest - BUF_HEADROOM) : 0;
  size_t cap_samples = avail / sizeof(int16_t);
  size_t n = (want < cap_samples) ? want : cap_samples;
  if (n < REC_RATE) {
    ESP_LOGE(TAG, "no RAM for mic buffer (largest free %u B)", (unsigned)largest);
    return false;
  }
  s_rec_buf = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (s_rec_buf == NULL)
    return false;
  s_rec_capacity = n;
  ESP_LOGI(TAG, "mic buffer: %u samples (~%u s)", (unsigned)n, (unsigned)(n / REC_RATE));
  return true;
}

static void normalize_pcm(int16_t *buf, size_t n, int target) {
  if (n == 0)
    return;
  int32_t peak = 1;
  for (size_t i = 0; i < n; i++) {
    int32_t a = buf[i] < 0 ? -(int32_t)buf[i] : buf[i];
    if (a > peak)
      peak = a;
  }
  int32_t gain_q8 = (target * 256) / peak;
  if (gain_q8 < 256)
    gain_q8 = 256;
  if (gain_q8 > NORM_MAX_GAIN_Q8)
    gain_q8 = NORM_MAX_GAIN_Q8;
  for (size_t i = 0; i < n; i++) {
    int32_t v = ((int32_t)buf[i] * gain_q8) >> 8;
    if (v > 32767)
      v = 32767;
    else if (v < -32768)
      v = -32768;
    buf[i] = (int16_t)v;
  }
}

static void scope_reset(void) {
  for (int i = 0; i < SCOPE_W; i++)
    s_scope[i] = 0;
  s_scope_head = 0;
}

static void scope_push(int peak) {
  int h = s_scope_head;
  s_scope[h] = (int16_t)(peak > 32767 ? 32767 : peak);
  s_scope_head = (h + 1) % SCOPE_W;
}

static void scope_redraw(void) {
  if (s_scope_line == NULL)
    return;
  int head = s_scope_head;
  for (int j = 0; j < SCOPE_W; j++) {
    int idx = (head + j) % SCOPE_W;
    float v = (float)s_scope[idx] / SCOPE_FULL;
    if (v > 1.0f)
      v = 1.0f;
    s_scope_pts[j].x = 6 + j * 178 / (SCOPE_W - 1);
    s_scope_pts[j].y = 40 - (int)(v * 34.0f);
  }
  lv_obj_invalidate(s_scope_line);
}

static void overlay_hide_cb(lv_timer_t *t) {
  lv_timer_delete(t);
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
    s_ov_label = NULL;
    s_ov_bar = NULL;
    s_scope_line = NULL;
  }
}

static void overlay_tick(lv_timer_t *t) {
  if (s_ov_bar == NULL) {
    lv_timer_delete(t);
    s_ov_timer = NULL;
    return;
  }
  if (s_op_done) {
    lv_bar_set_value(s_ov_bar, 100, LV_ANIM_OFF);
    if (s_ov_label)
      lv_label_set_text(s_ov_label, s_done_text);
    if (s_scope_line)
      lv_obj_add_flag(s_scope_line, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(t);
    s_ov_timer = NULL;
    lv_timer_t *h = lv_timer_create(overlay_hide_cb, OVERLAY_HIDE_MS, NULL);
    lv_timer_set_repeat_count(h, 1);
    return;
  }
  uint32_t elapsed = lv_tick_get() - s_ov_start;
  if (s_ov_mode == OV_VU) {
    int pct = (int)((int64_t)s_live_peak * 100 / VU_FULLSCALE_PEAK);
    if (pct > 100)
      pct = 100;
    if (pct > s_vu_display)
      s_vu_display = pct;
    else
      s_vu_display = (s_vu_display * 7) / 10;
    lv_bar_set_value(s_ov_bar, s_vu_display, LV_ANIM_OFF);
    scope_redraw();
    int remain = (int)((s_ov_total > elapsed) ? (s_ov_total - elapsed + 999) / 1000 : 0);
    if (s_ov_label) {
      int rms = s_live_rms < 1 ? 1 : s_live_rms;
      int db = (int)(20.0f * log10f((float)rms / 32768.0f));
      char buf[40];
      snprintf(buf, sizeof(buf), "RECORDING  %ds\n%d dBFS", remain, db);
      lv_label_set_text(s_ov_label, buf);
    }
  } else {
    int pct = (int)((uint64_t)elapsed * 100 / s_ov_total);
    if (pct > 99)
      pct = 99;
    lv_bar_set_value(s_ov_bar, pct, LV_ANIM_OFF);
  }
}

static void overlay_show(const char *title, uint32_t total_ms, int mode) {
  if (s_ov == NULL) {
    s_ov = lv_obj_create(s_screen);
    lv_obj_set_size(s_ov, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_ov);
    lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_ov, 0, 0);

    lv_obj_t *box = lv_obj_create(s_ov);
    lv_obj_set_size(box, 210, 132);
    lv_obj_center(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 10, 0);

    s_ov_label = lv_label_create(box);
    lv_obj_set_style_text_color(s_ov_label, current_theme.text_main, 0);
    lv_obj_set_style_text_align(s_ov_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ov_label, LV_ALIGN_TOP_MID, 0, 2);

    s_scope_line = lv_line_create(box);
    lv_obj_set_style_line_color(s_scope_line, ui_theme_get_accent(), 0);
    lv_obj_set_style_line_width(s_scope_line, 2, 0);
    lv_obj_set_pos(s_scope_line, 0, 0);
    for (int j = 0; j < SCOPE_W; j++) {
      s_scope_pts[j].x = 6 + j * 178 / (SCOPE_W - 1);
      s_scope_pts[j].y = 40;
    }
    lv_line_set_points_mutable(s_scope_line, s_scope_pts, SCOPE_W);

    s_ov_bar = lv_bar_create(box);
    lv_obj_set_size(s_ov_bar, 180, 16);
    lv_obj_align(s_ov_bar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_bar_set_range(s_ov_bar, 0, 100);
    lv_obj_set_style_bg_color(s_ov_bar, lv_color_hex(0x202028), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ov_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ov_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ov_bar, lv_color_hex(ACCENT_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ov_bar, 4, LV_PART_INDICATOR);
  }
  lv_label_set_text(s_ov_label, title);
  lv_bar_set_value(s_ov_bar, 0, LV_ANIM_OFF);

  if (s_scope_line) {
    if (mode == OV_VU)
      lv_obj_remove_flag(s_scope_line, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_scope_line, LV_OBJ_FLAG_HIDDEN);
  }
  s_ov_mode = mode;
  s_ov_start = lv_tick_get();
  s_ov_total = total_ms ? total_ms : 1;
  s_op_done = false;
  s_vu_display = 0;
  if (s_ov_timer == NULL)
    s_ov_timer = lv_timer_create(overlay_tick, OVERLAY_TICK_MS, NULL);
}

static void op_done_cb(void *unused) {
  (void)unused;
  s_op_done = true;
  s_state = ST_IDLE;
}

static void finish(const char *done_text) {
  strncpy(s_done_text, done_text, sizeof(s_done_text) - 1);
  s_done_text[sizeof(s_done_text) - 1] = '\0';
  lv_async_call(op_done_cb, NULL);
}

static void mic_level_cb(int peak, int rms, void *ctx) {
  (void)ctx;
  s_live_peak = peak;
  s_live_rms = rms;
  scope_push(peak);
}

static void record_task(void *arg) {
  (void)arg;
  size_t got = 0;
  audio_i2s_mic_record(s_rec_buf, s_rec_capacity, REC_RATE, &got, mic_level_cb, NULL);
  normalize_pcm(s_rec_buf, got, s_rec_target);
  s_rec_samples = got;
  s_last_ms = (uint32_t)((uint64_t)got * 1000 / REC_RATE);
  finish(got > 0 ? "Recorded!" : "Mic failed");
  s_busy = false;
  vTaskDelete(NULL);
}

static void play_task(void *arg) {
  (void)arg;
  do {
    audio_i2s_play_pcm(s_rec_buf, s_rec_samples, REC_RATE);
  } while (s_loop && !s_stop_req);
  finish("Done");
  s_busy = false;
  vTaskDelete(NULL);
}

static void activate(int idx) {
  if (s_busy)
    return;
  if (idx == ROW_REC) {
    if (!ensure_buffer()) {
      overlay_show("No memory", 1, OV_TIME);
      finish("No memory");
      return;
    }
    s_live_peak = 0;
    s_live_rms = 0;
    scope_reset();
    s_rec_target =
        REC_TARGET_MIN + menu_component_get_intensity(&s_menu, ROW_LEVEL) * REC_TARGET_STEP;
    s_state = ST_RECORDING;
    uint32_t dur_ms = (uint32_t)(s_rec_capacity / REC_RATE) * 1000 + 250;
    overlay_show("RECORDING...", dur_ms, OV_VU);
    s_busy = true;
    if (xTaskCreate(record_task, "mic_rec", MIC_TASK_STACK, NULL, MIC_TASK_PRIORITY, NULL) !=
        pdPASS) {
      s_busy = false;
      s_state = ST_IDLE;
      finish("Task error");
    }
  } else if (idx == ROW_PLAY) {
    if (s_rec_samples == 0) {
      overlay_show("Record first", 1, OV_TIME);
      finish("Record first");
      return;
    }
    s_loop = menu_component_get_toggle(&s_menu, ROW_LOOP);
    s_stop_req = false;
    s_state = ST_PLAYING;
    uint32_t dur_ms = (uint32_t)(s_rec_samples / REC_RATE) * 1000 + 150;
    overlay_show(s_loop ? "PLAYING (loop)" : "PLAYING...", dur_ms, OV_TIME);
    s_busy = true;
    if (xTaskCreate(play_task, "mic_play", MIC_TASK_STACK, NULL, MIC_TASK_PRIORITY, NULL) !=
        pdPASS) {
      s_busy = false;
      s_state = ST_IDLE;
      finish("Task error");
    }
  }
}

static void refresh_status(void) {
  if (s_status_lbl == NULL)
    return;
  char buf[40];
  if (s_state == ST_RECORDING)
    snprintf(buf, sizeof(buf), "RECORDING...");
  else if (s_state == ST_PLAYING)
    snprintf(buf, sizeof(buf), s_loop ? "PLAYING (loop)" : "PLAYING...");
  else if (s_rec_samples > 0)
    snprintf(buf,
             sizeof(buf),
             "IDLE   Last: %u.%us",
             (unsigned)(s_last_ms / 1000),
             (unsigned)((s_last_ms % 1000) / 100));
  else
    snprintf(buf, sizeof(buf), "IDLE   (no recording)");
  lv_label_set_text(s_status_lbl, buf);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up(), down = ui_btn_down();
  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (s_ov != NULL) {
    if (back && !s_btn_back_last && s_busy && s_state == ST_PLAYING)
      s_stop_req = true;
    s_btn_up_last = up;
    s_btn_down_last = down;
    s_btn_left_last = left;
    s_btn_right_last = right;
    s_btn_ok_last = ok;
    s_btn_back_last = back;
    return;
  }

  refresh_status();

  int sel = menu_component_get_selected(&s_menu);
  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);
  if (right && !s_btn_right_last && sel == ROW_LEVEL)
    menu_component_intensity_inc(&s_menu, ROW_LEVEL);
  if (left && !s_btn_left_last && sel == ROW_LEVEL)
    menu_component_intensity_dec(&s_menu, ROW_LEVEL);
  if (ok && !s_btn_ok_last) {
    if (sel == ROW_LOOP)
      menu_component_toggle_item(&s_menu, ROW_LOOP);
    else
      activate(sel);
  }
  if (back && !s_btn_back_last)
    ui_switch_screen(SCREEN_SETTINGS);

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_micrec_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ov = NULL;
  s_ov_label = NULL;
  s_ov_bar = NULL;
  s_scope_line = NULL;
  s_ov_timer = NULL;
  s_state = ST_IDLE;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "Mic -> Speaker", "/assets/icons/mic.bin");
  menu_component_add_intensity(&s_menu, "/assets/icons/graphic_eq.bin", "Rec Level", 3);
  menu_component_add_item(&s_menu, "/assets/icons/fiber_manual_record.bin", "Record");
  menu_component_add_item(&s_menu, "/assets/icons/play_arrow.bin", "Play");
  menu_component_add_toggle(&s_menu, "/assets/icons/repeat.bin", "Loop", false);

  s_status_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_status_lbl, "IDLE   (no recording)");
  lv_obj_set_style_text_color(s_status_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_status_lbl, LV_OPA_70, 0);
  lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -4 - MENU_COMP_FOOTER_H);
  refresh_status();

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
  ESP_LOGI(TAG, "mic-rec menu opened");
}

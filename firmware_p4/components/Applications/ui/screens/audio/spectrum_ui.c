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

#include "spectrum_ui.h"

#include <math.h>
#include <stdlib.h>

#include "esp_dsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"
#include "st7789.h"

#include "audio_i2s.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "SPECTRUM_UI";

#define SAMPLE_RATE            16000
#define FFT_N                  512
#define N_BARS                 24
#define BIN_LO                 2
#define BIN_HI                 200
#define GMAX_FLOOR             6000.0f
#define NOISE_GATE             4000.0f
#define GMAX_DECAY             0.95f
#define CAP_H                  3
#define HOLD_FRAMES            12
#define PEAK_FALL              0.015f
#define CLIP_SAMPLE            32000
#define PEAK_DB_FLOOR          (-60.0f)
#define TASK_STOP_RETRIES      40
#define TASK_STOP_DELAY_MS     10
#define ANIM_TIMER_MS          33
#define SPECTRUM_TASK_STACK 8192
#define SPECTRUM_TASK_PRIORITY SYS_PRIO_SERVICE_LO
#define SPECTRUM_TASK_CORE SYS_CORE_UI

#define SPECTRUM_ICON      "/assets/icons/graphic_eq.bin"
#define SPECTRUM_TITLE     "SPECTRUM"
#define SPECTRUM_FOOTER    "BACK to exit"
#define STATUS_ROW_TOP_OFS 4
#define PLOT_TOP_OFS       24
#define AXIS_H             16
#define PLOT_BOTTOM_PAD    4
#define PLOT_MIN_H         40

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_plot = NULL;
static lv_obj_t *s_bars[N_BARS];
static lv_obj_t *s_caps[N_BARS];
static lv_obj_t *s_db_label = NULL;
static lv_obj_t *s_clip_label = NULL;
static lv_timer_t *s_anim_timer = NULL;

static volatile float s_bands[N_BARS];
static volatile float s_peak_db = PEAK_DB_FLOOR;
static volatile bool s_clip = false;
static volatile bool s_running = false;
static volatile bool s_task_active = false;
static float s_gmax = GMAX_FLOOR;
static float s_disp[N_BARS];
static float s_peak[N_BARS];
static uint8_t s_hold[N_BARS];
static int s_plot_h = 100;

static void spectrum_task(void *arg) {
  (void)arg;
  s_task_active = true;

  int16_t *raw = malloc(FFT_N * sizeof(int16_t));
  float *y = malloc(2 * FFT_N * sizeof(float));
  float *win = malloc(FFT_N * sizeof(float));
  if (!raw || !y || !win) {
    ESP_LOGE(TAG, "FFT buffer alloc failed");
    goto done;
  }

  static bool dsp_inited = false;
  if (!dsp_inited && dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE) == ESP_OK)
    dsp_inited = true;
  dsps_wind_hann_f32(win, FFT_N);

  int blo[N_BARS], bhi[N_BARS];
  for (int i = 0; i < N_BARS; i++) {
    float e0 = (float)BIN_LO * powf((float)BIN_HI / BIN_LO, (float)i / N_BARS);
    float e1 = (float)BIN_LO * powf((float)BIN_HI / BIN_LO, (float)(i + 1) / N_BARS);
    blo[i] = (int)(e0 + 0.5f);
    bhi[i] = (int)(e1 + 0.5f);
    if (bhi[i] <= blo[i])
      bhi[i] = blo[i] + 1;
    if (bhi[i] > FFT_N / 2)
      bhi[i] = FFT_N / 2;
  }

  if (audio_i2s_mic_stream_start(SAMPLE_RATE) != ESP_OK) {
    ESP_LOGE(TAG, "mic stream start failed");
    goto done;
  }

  while (s_running) {
    int filled = 0;
    while (s_running && filled < FFT_N) {
      int got = audio_i2s_mic_stream_read(raw + filled, FFT_N - filled);
      if (got <= 0)
        break;
      filled += got;
    }
    if (filled < FFT_N)
      continue;

    int rawpk = 0;
    for (int i = 0; i < FFT_N; i++) {
      int a = raw[i] < 0 ? -raw[i] : raw[i];
      if (a > rawpk)
        rawpk = a;
      y[2 * i] = (float)raw[i] * win[i];
      y[2 * i + 1] = 0.0f;
    }

    s_peak_db = rawpk > 0 ? 20.0f * log10f((float)rawpk / 32768.0f) : PEAK_DB_FLOOR;
    if (s_peak_db < PEAK_DB_FLOOR)
      s_peak_db = PEAK_DB_FLOOR;
    s_clip = (rawpk >= CLIP_SAMPLE);

    if (dsp_inited) {
      dsps_fft2r_fc32(y, FFT_N);
      dsps_bit_rev_fc32(y, FFT_N);
      dsps_cplx2reC_fc32(y, FFT_N);
    }

    float mag[N_BARS];
    float fmax = 1.0f;
    for (int b = 0; b < N_BARS; b++) {
      float p = 0.0f;
      for (int k = blo[b]; k < bhi[b]; k++) {
        float re = y[2 * k], im = y[2 * k + 1];
        p += re * re + im * im;
      }
      float m = sqrtf(p / (float)(bhi[b] - blo[b]));
      mag[b] = m;
      if (m > fmax)
        fmax = m;
    }

    s_gmax *= GMAX_DECAY;
    if (fmax > s_gmax)
      s_gmax = fmax;
    if (s_gmax < GMAX_FLOOR)
      s_gmax = GMAX_FLOOR;
    bool gate = (fmax < NOISE_GATE);
    for (int b = 0; b < N_BARS; b++) {
      float n = gate ? 0.0f : (mag[b] / s_gmax);
      if (n > 1.0f)
        n = 1.0f;
      s_bands[b] = sqrtf(n);
    }
  }

  audio_i2s_mic_stream_stop();

done:
  free(raw);
  free(y);
  free(win);
  for (int b = 0; b < N_BARS; b++)
    s_bands[b] = 0.0f;
  s_running = false;
  s_task_active = false;
  vTaskDelete(NULL);
}

static void spectrum_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->button == INPUT_BTN_BACK && ev->action == INPUT_ACTION_PRESS) {
    s_running = false;
    ui_switch_screen(SCREEN_SETTINGS);
  }
}

static void anim_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    s_running = false;
    lv_timer_delete(t);
    s_anim_timer = NULL;
    return;
  }

  for (int i = 0; i < N_BARS; i++) {
    float b = s_bands[i];
    if (b > s_disp[i])
      s_disp[i] = b;
    else
      s_disp[i] = s_disp[i] * 0.80f + b * 0.20f;
    int h = (int)(2.0f + s_disp[i] * 96.0f);
    lv_obj_set_height(s_bars[i], lv_pct(h));

    if (s_disp[i] >= s_peak[i]) {
      s_peak[i] = s_disp[i];
      s_hold[i] = HOLD_FRAMES;
    } else if (s_hold[i] > 0) {
      s_hold[i]--;
    } else {
      s_peak[i] -= PEAK_FALL;
      if (s_peak[i] < s_disp[i])
        s_peak[i] = s_disp[i];
    }
    int cy = s_plot_h - (int)(s_peak[i] * (float)s_plot_h) - CAP_H;
    if (cy < 0)
      cy = 0;
    if (cy > s_plot_h - CAP_H)
      cy = s_plot_h - CAP_H;
    lv_obj_set_y(s_caps[i], cy);
  }

  if (s_db_label)
    lv_label_set_text_fmt(s_db_label, "%d dBFS", (int)s_peak_db);
  if (s_clip_label) {
    if (s_clip)
      lv_obj_remove_flag(s_clip_label, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_clip_label, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_spectrum_open(void) {
  s_running = false;
  for (int i = 0; i < TASK_STOP_RETRIES && s_task_active; i++)
    vTaskDelay(pdMS_TO_TICKS(TASK_STOP_DELAY_MS));

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  for (int i = 0; i < N_BARS; i++) {
    s_disp[i] = 0.0f;
    s_bands[i] = 0.0f;
    s_peak[i] = 0.0f;
    s_hold[i] = 0;
  }
  s_gmax = GMAX_FLOOR;
  s_peak_db = PEAK_DB_FLOOR;
  s_clip = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, SPECTRUM_TITLE, SPECTRUM_ICON);
  ui_chrome_footer(s_screen, SPECTRUM_FOOTER);

  int band_top = UI_CHROME_HEADER_H;
  int plot_top = band_top + PLOT_TOP_OFS;
  int plot_h = LCD_V_RES - UI_CHROME_FOOTER_H - AXIS_H - PLOT_BOTTOM_PAD - plot_top;
  if (plot_h < PLOT_MIN_H)
    plot_h = PLOT_MIN_H;

  s_db_label = lv_label_create(s_screen);
  lv_label_set_text(s_db_label, "-60 dBFS");
  lv_obj_set_style_text_color(s_db_label, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_db_label, LV_OPA_70, 0);
  lv_obj_set_style_text_font(s_db_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_db_label, LV_ALIGN_TOP_RIGHT, -8, band_top + STATUS_ROW_TOP_OFS);

  s_clip_label = lv_label_create(s_screen);
  lv_label_set_text(s_clip_label, "CLIP");
  lv_obj_set_style_text_color(s_clip_label, lv_color_hex(0xFF5252), 0);
  lv_obj_set_style_text_font(s_clip_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_clip_label, LV_ALIGN_TOP_LEFT, 8, band_top + STATUS_ROW_TOP_OFS);
  lv_obj_add_flag(s_clip_label, LV_OBJ_FLAG_HIDDEN);

  s_plot = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_plot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_plot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_plot, 0, 0);
  lv_obj_set_style_pad_all(s_plot, 4, 0);
  lv_obj_set_style_pad_column(s_plot, 3, 0);
  lv_obj_set_size(s_plot, lv_pct(94), plot_h);
  lv_obj_align(s_plot, LV_ALIGN_TOP_MID, 0, plot_top);
  lv_obj_set_flex_flow(s_plot, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_plot, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  for (int g = 1; g <= 3; g++) {
    lv_obj_t *line = lv_obj_create(s_plot);
    lv_obj_add_flag(line, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_bg_color(line, current_theme.text_main, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_20, 0);
    lv_obj_set_size(line, lv_pct(100), 1);
    lv_obj_set_align(line, LV_ALIGN_TOP_MID);
    lv_obj_set_y(line, lv_pct(g * 25));
  }

  for (int i = 0; i < N_BARS; i++) {
    lv_obj_t *bar = lv_obj_create(s_plot);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(bar, 1);
    lv_obj_set_height(bar, lv_pct(2));
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF5252), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0x00E676), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    s_bars[i] = bar;
  }

  for (int i = 0; i < N_BARS; i++) {
    lv_obj_t *cap = lv_obj_create(s_plot);
    lv_obj_add_flag(cap, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_radius(cap, 1, 0);
    lv_obj_set_style_bg_color(cap, current_theme.text_main, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_80, 0);
    lv_obj_set_size(cap, 4, CAP_H);
    s_caps[i] = cap;
  }

  static const char *const FAXIS[] = {"60", "250", "1k", "4k"};
  lv_obj_t *axis = lv_obj_create(s_screen);
  lv_obj_remove_flag(axis, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(axis, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(axis, 0, 0);
  lv_obj_set_style_pad_all(axis, 0, 0);
  lv_obj_set_size(axis, lv_pct(94), 16);
  lv_obj_align_to(axis, s_plot, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
  lv_obj_set_flex_flow(axis, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      axis, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *l = lv_label_create(axis);
    lv_label_set_text(l, FAXIS[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, current_theme.text_main, 0);
    lv_obj_set_style_text_opa(l, LV_OPA_50, 0);
  }

  lv_obj_update_layout(s_screen);
  s_plot_h = lv_obj_get_content_height(s_plot);
  if (s_plot_h < 10)
    s_plot_h = 100;
  for (int i = 0; i < N_BARS; i++) {
    int bw = lv_obj_get_width(s_bars[i]);
    int bx = lv_obj_get_x(s_bars[i]);
    lv_obj_set_size(s_caps[i], bw > 0 ? bw : 4, CAP_H);
    lv_obj_set_x(s_caps[i], bx);
    lv_obj_set_y(s_caps[i], s_plot_h - CAP_H);
  }

  s_running = true;
  if (xTaskCreatePinnedToCore(spectrum_task,
                              "spectrum",
                              SPECTRUM_TASK_STACK,
                              NULL,
                              SPECTRUM_TASK_PRIORITY,
                              NULL,
                              SPECTRUM_TASK_CORE) != pdPASS) {
    ESP_LOGE(TAG, "spectrum task create failed");
    s_running = false;
  }

  if (s_anim_timer == NULL)
    s_anim_timer = lv_timer_create(anim_timer_cb, ANIM_TIMER_MS, NULL);

  ui_input_set_screen_handler(spectrum_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "spectrum analyzer opened");
}

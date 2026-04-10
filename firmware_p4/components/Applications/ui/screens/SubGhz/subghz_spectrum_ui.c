// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "subghz_spectrum_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "core/lv_group.h"
#include "lv_conf_internal.h"

#include "ui_theme.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "subghz_spectrum.h"

static const char *TAG = "SUBGHZ_SPECTRUM_UI";

#define SPECTRUM_CENTER_FREQ 433920000
#define SPECTRUM_SPAN_HZ     2000000

#define CHART_W           220
#define CHART_H           120
#define CHART_OFFSET_Y    (-10)
#define CHART_BORDER_W    1
#define CHART_LINE_W      2
#define CHART_RANGE_MIN   0
#define CHART_RANGE_MAX   100
#define CHART_ITEM_BG_OPA 80

#define RSSI_CLAMP_MIN          0
#define RSSI_CLAMP_MAX          100
#define RSSI_FLOOR_DBM          (-130.0f)
#define RSSI_PEAK_THRESHOLD_DBM (-60.0f)

#define LABEL_OFFSET_Y         (-35)
#define UPDATE_TIMER_PERIOD_MS 50

#define PEAK_LABEL_DEFAULT  "Peak: --- dBm"
#define FREQ_LABEL_DEFAULT  "433.92 MHz"
#define PEAK_LABEL_FMT      "Peak: %.1f dBm"
#define FREQ_LABEL_FMT      "%.2f MHz"
#define PEAK_LABEL_BUF_SIZE 32
#define FREQ_LABEL_BUF_SIZE 32

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_ser_rssi = NULL;
static lv_timer_t *s_update_timer = NULL;
static lv_obj_t *s_lbl_rssi = NULL;
static lv_obj_t *s_lbl_freq = NULL;

static int32_t s_chart_points[SPECTRUM_SAMPLES];

static void update_spectrum_cb(lv_timer_t *t);
static void on_screen_key_event(lv_event_t *e);

static void update_spectrum_cb(lv_timer_t *t) {
  if (s_chart == NULL || s_ser_rssi == NULL)
    return;

  subghz_spectrum_line_t line;
  if (!subghz_spectrum_get_line(&line))
    return;

  float max_dbm = RSSI_FLOOR_DBM;
  uint32_t peak_freq = line.start_freq;

  for (int i = 0; i < SPECTRUM_SAMPLES; i++) {
    int32_t val = (int32_t)(line.dbm_values[i] + (-RSSI_FLOOR_DBM));
    if (val < RSSI_CLAMP_MIN)
      val = RSSI_CLAMP_MIN;
    if (val > RSSI_CLAMP_MAX)
      val = RSSI_CLAMP_MAX;
    s_chart_points[i] = val;

    if (line.dbm_values[i] > max_dbm) {
      max_dbm = line.dbm_values[i];
      peak_freq = line.start_freq + (uint32_t)(i * line.step_hz);
    }
  }

  lv_chart_set_ext_y_array(s_chart, s_ser_rssi, s_chart_points);
  lv_chart_refresh(s_chart);

  if (s_lbl_rssi != NULL) {
    char buf[PEAK_LABEL_BUF_SIZE];
    snprintf(buf, sizeof(buf), PEAK_LABEL_FMT, max_dbm);
    lv_label_set_text(s_lbl_rssi, buf);
  }

  if (s_lbl_freq != NULL) {
    char buf[FREQ_LABEL_BUF_SIZE];
    snprintf(buf, sizeof(buf), FREQ_LABEL_FMT, (double)(peak_freq / 1000000.0f));
    lv_label_set_text(s_lbl_freq, buf);
  }
}

static void on_screen_key_event(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;

  if (lv_event_get_key(e) != LV_KEY_ESC)
    return;

  if (s_update_timer != NULL) {
    lv_timer_del(s_update_timer);
    s_update_timer = NULL;
  }

  s_chart = NULL;
  s_ser_rssi = NULL;
  s_lbl_rssi = NULL;
  s_lbl_freq = NULL;

  subghz_spectrum_stop();
  ui_switch_screen(SCREEN_MENU);
}

void ui_subghz_spectrum_open(void) {
  subghz_spectrum_start(SPECTRUM_CENTER_FREQ, SPECTRUM_SPAN_HZ);

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  if (s_update_timer != NULL) {
    lv_timer_del(s_update_timer);
    s_update_timer = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_chart = lv_chart_create(s_screen);
  lv_obj_set_size(s_chart, CHART_W, CHART_H);
  lv_obj_align(s_chart, LV_ALIGN_CENTER, 0, CHART_OFFSET_Y);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_chart, SPECTRUM_SAMPLES);
  lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, CHART_RANGE_MIN, CHART_RANGE_MAX);
  lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_CIRCULAR);
  lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(s_chart, CHART_LINE_W, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_chart, current_theme.bg_primary, 0);
  lv_obj_set_style_border_color(s_chart, current_theme.border_interface, 0);
  lv_obj_set_style_border_width(s_chart, CHART_BORDER_W, 0);
  lv_obj_set_style_bg_opa(s_chart, CHART_ITEM_BG_OPA, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_chart, current_theme.border_accent, LV_PART_ITEMS);
  lv_obj_set_style_bg_grad_color(s_chart, current_theme.bg_secondary, LV_PART_ITEMS);
  lv_obj_set_style_bg_grad_dir(s_chart, LV_GRAD_DIR_VER, LV_PART_ITEMS);
  lv_obj_set_style_line_dash_width(s_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_line_color(s_chart, current_theme.border_inactive, LV_PART_MAIN);

  s_ser_rssi = lv_chart_add_series(s_chart, current_theme.border_accent, LV_CHART_AXIS_PRIMARY_Y);

  s_lbl_freq = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_freq, FREQ_LABEL_DEFAULT);
  lv_obj_set_style_text_font(s_lbl_freq, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_lbl_freq, current_theme.text_main, 0);
  lv_obj_align(s_lbl_freq, LV_ALIGN_BOTTOM_LEFT, 0, LABEL_OFFSET_Y);

  s_lbl_rssi = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_rssi, PEAK_LABEL_DEFAULT);
  lv_obj_set_style_text_font(s_lbl_rssi, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_lbl_rssi, current_theme.border_accent, 0);
  lv_obj_align(s_lbl_rssi, LV_ALIGN_BOTTOM_RIGHT, 0, LABEL_OFFSET_Y);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_update_timer = lv_timer_create(update_spectrum_cb, UPDATE_TIMER_PERIOD_MS, NULL);

  lv_obj_add_event_cb(s_screen, on_screen_key_event, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen);
    lv_group_focus_obj(s_screen);
  }

  lv_screen_load(s_screen);
}
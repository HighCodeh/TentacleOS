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

#include "wifi_scan_monitor_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_sniffer.h"

static const char *TAG = "UI_SCAN_MONITOR";

/* ---- Chart constants ---- */
#define CHART_W           220
#define CHART_H           110
#define CHART_OFFSET_Y    (-10)
#define CHART_POINT_COUNT 80
#define CHART_MAX_VAL     500
#define CHART_BORDER_W    1

/* ---- Label layout ---- */
#define LABEL_MARGIN_X 6
#define LABEL_OFFSET_Y (-35)

/* ---- Timer ---- */
#define UPDATE_TIMER_MS 1000

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_ser_pps = NULL;
static lv_timer_t *s_update_timer = NULL;
static lv_obj_t *s_lbl_pkts = NULL;
static lv_obj_t *s_lbl_deauth = NULL;

static uint32_t s_last_pkt_count = 0;

extern lv_group_t *main_group;

static void update_monitor_cb(lv_timer_t *t) {
  (void)t;
  uint32_t pkt_count = wifi_sniffer_get_packet_count();
  uint32_t deauth_count = wifi_sniffer_get_deauth_count();

  uint32_t pps = (pkt_count >= s_last_pkt_count) ? (pkt_count - s_last_pkt_count) : pkt_count;
  s_last_pkt_count = pkt_count;

  if (s_chart != NULL && s_ser_pps != NULL) {
    if (pps > CHART_MAX_VAL)
      pps = CHART_MAX_VAL;
    lv_chart_set_next_value(s_chart, s_ser_pps, pps);
  }

  if (s_lbl_pkts != NULL) {
    lv_label_set_text_fmt(s_lbl_pkts, "Pkts: %lu", (unsigned long)pkt_count);
  }

  if (s_lbl_deauth != NULL) {
    lv_label_set_text_fmt(s_lbl_deauth, "Deauths: %lu", (unsigned long)deauth_count);
    if (deauth_count > 0) {
      lv_obj_set_style_text_color(s_lbl_deauth, current_theme.border_inactive, 0);
    } else {
      lv_obj_set_style_text_color(s_lbl_deauth, current_theme.text_main, 0);
    }
  }
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
      if (s_update_timer != NULL) {
        lv_timer_del(s_update_timer);
        s_update_timer = NULL;
      }
      wifi_sniffer_stop();
      wifi_sniffer_free_buffer();
      ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
    }
  }
}

void ui_wifi_scan_monitor_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);
  if (s_update_timer != NULL) {
    lv_timer_del(s_update_timer);
    s_update_timer = NULL;
  }

  wifi_sniffer_start(WIFI_SNIFFER_TYPE_RAW, 0);
  s_last_pkt_count = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_chart = lv_chart_create(s_screen);
  lv_obj_set_size(s_chart, CHART_W, CHART_H);
  lv_obj_align(s_chart, LV_ALIGN_CENTER, 0, CHART_OFFSET_Y);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_chart, CHART_POINT_COUNT);

  lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_MAX_VAL);
  lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);

  lv_obj_set_style_bg_color(s_chart, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_chart, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_chart, CHART_BORDER_W, 0);
  lv_obj_set_style_radius(s_chart, 0, 0);

  lv_obj_set_style_line_width(s_chart, CHART_BORDER_W, LV_PART_ITEMS);
  lv_obj_set_style_line_color(s_chart, current_theme.border_accent, LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(s_chart, false, LV_PART_ITEMS);

  lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(s_chart, 0, LV_PART_INDICATOR);

  lv_obj_set_style_line_color(s_chart, current_theme.border_inactive, LV_PART_MAIN);
  lv_obj_set_style_line_width(s_chart, CHART_BORDER_W, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(s_chart, false, LV_PART_MAIN);

  s_ser_pps = lv_chart_add_series(s_chart, current_theme.border_accent, LV_CHART_AXIS_PRIMARY_Y);

  s_lbl_pkts = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_pkts, "Pkts: 0");
  lv_obj_set_style_text_color(s_lbl_pkts, current_theme.text_main, 0);
  lv_obj_align(s_lbl_pkts, LV_ALIGN_BOTTOM_LEFT, LABEL_MARGIN_X, LABEL_OFFSET_Y);

  s_lbl_deauth = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_deauth, "Deauths: 0");
  lv_obj_set_style_text_color(s_lbl_deauth, current_theme.text_main, 0);
  lv_obj_align(s_lbl_deauth, LV_ALIGN_BOTTOM_RIGHT, -LABEL_MARGIN_X, LABEL_OFFSET_Y);

  s_update_timer = lv_timer_create(update_monitor_cb, UPDATE_TIMER_MS, NULL);
  update_monitor_cb(s_update_timer);

  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_chart, screen_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen);
    lv_group_focus_obj(s_screen);
  }

  lv_screen_load(s_screen);
}

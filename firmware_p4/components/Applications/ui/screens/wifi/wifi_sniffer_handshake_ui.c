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

#include "wifi_sniffer_handshake_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "storage_impl.h"
#include "tos_loot.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_sniffer.h"

static const char *TAG = "UI_SNIFFER_HS";

#define TERMINAL_W        230
#define TERMINAL_H        125
#define TERMINAL_BORDER_W 1
#define UPDATE_TIMER_MS   500
#define FILENAME_MAX      64
#define FILEPATH_MAX      128
#define TERMINAL_TEXT_MAX 192

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_ta_term = NULL;
static lv_timer_t *s_update_timer = NULL;
static bool s_is_scanning = false;
static bool s_is_capture_done = false;
static char s_current_filename[FILENAME_MAX];
static char s_current_path[FILEPATH_MAX];

extern lv_group_t *main_group;

static void generate_filename(void) {
  tos_loot_generate_path(TOS_PATH_WIFI_LOOT_HS,
                         "handshake",
                         "pcap",
                         s_current_path,
                         sizeof(s_current_path),
                         s_current_filename,
                         sizeof(s_current_filename));
}

static void update_terminal_text(void) {
  if (s_ta_term == NULL)
    return;
  const char *state =
      s_is_capture_done ? "HANDSHAKE CAPTURED (M1+M2)" : "Waiting for connection...";
  char text[TERMINAL_TEXT_MAX];
  snprintf(text,
           sizeof(text),
           "Mode: EAPOL Monitor\n"
           "File: %s\n"
           "%s\n"
           "Pkts: %lu\n",
           s_current_filename,
           state,
           (unsigned long)wifi_sniffer_get_packet_count());
  lv_textarea_set_text(s_ta_term, text);
}

static void stop_sniffer(void) {
  if (s_update_timer != NULL) {
    lv_timer_del(s_update_timer);
    s_update_timer = NULL;
  }
  if (s_is_scanning) {
    wifi_sniffer_stop();
    s_is_scanning = false;
  }
  wifi_sniffer_free_buffer();
}

static void update_timer_cb(lv_timer_t *t) {
  (void)t;
  update_terminal_text();

  if (!s_is_capture_done && wifi_sniffer_handshake_captured()) {
    wifi_sniffer_stop();
    s_is_scanning = false;
    s_is_capture_done = true;
    wifi_sniffer_free_buffer();
    update_terminal_text();
  }
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    stop_sniffer();
    ui_switch_screen(SCREEN_WIFI_PACKETS_MENU);
  }
}

void ui_wifi_sniffer_handshake_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);
  if (s_update_timer != NULL) {
    lv_timer_del(s_update_timer);
    s_update_timer = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_ta_term = lv_textarea_create(s_screen);
  lv_obj_set_size(s_ta_term, TERMINAL_W, TERMINAL_H);
  lv_obj_align(s_ta_term, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_font(s_ta_term, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_ta_term, current_theme.screen_base, 0);
  lv_obj_set_style_text_color(s_ta_term, current_theme.border_accent, 0);
  lv_obj_set_style_radius(s_ta_term, 0, 0);
  lv_obj_set_style_border_width(s_ta_term, TERMINAL_BORDER_W, 0);
  lv_obj_set_style_border_color(s_ta_term, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_ta_term, TERMINAL_BORDER_W, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_color(s_ta_term, current_theme.border_accent, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(s_ta_term, 0, LV_STATE_FOCUS_KEY);

  generate_filename();
  wifi_sniffer_clear_handshake();
  s_is_capture_done = false;

  wifi_sniffer_start_capture(s_current_path);
  if (wifi_sniffer_start_stream(WIFI_SNIFFER_TYPE_EAPOL, 0, NULL)) {
    s_is_scanning = true;
  } else {
    s_is_scanning = false;
    wifi_sniffer_stop_capture();
  }

  update_terminal_text();
  s_update_timer = lv_timer_create(update_timer_cb, UPDATE_TIMER_MS, NULL);

  lv_obj_add_event_cb(s_ta_term, screen_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_ta_term);
    lv_group_focus_obj(s_ta_term);
  }

  lv_screen_load(s_screen);
}

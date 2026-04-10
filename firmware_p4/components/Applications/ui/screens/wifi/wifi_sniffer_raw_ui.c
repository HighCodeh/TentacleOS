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

#include "wifi_sniffer_raw_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "msgbox_ui.h"
#include "storage_impl.h"
#include "storage_init.h"
#include "tos_loot.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_sniffer.h"

static const char *TAG = "UI_SNIFFER_RAW";

/* ---- Layout constants ---- */
#define TERMINAL_W        230
#define TERMINAL_H        125
#define TERMINAL_Y        30
#define TERMINAL_BORDER_W 1
#define BTN_SAVE_W        120
#define BTN_SAVE_H        28
#define BTN_SAVE_Y        (-32)
#define BTN_SAVE_BORDER_W 1
#define BTN_SAVE_FOCUS_BW 2
#define BTN_SAVE_RADIUS   4

/* ---- Timer / sizing ---- */
#define UPDATE_TIMER_MS   500
#define FILENAME_MAX      64
#define FILEPATH_MAX      128
#define TERMINAL_TEXT_MAX 192
#define BYTES_PER_KB      1024
#define DOT_ANIM_COUNT    4
#define MB_FRAC_SCALE     100

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_ta_term = NULL;
static lv_obj_t *s_btn_save = NULL;
static lv_timer_t *s_update_timer = NULL;
static char s_current_filename[FILENAME_MAX];
static char s_current_path[FILEPATH_MAX];
static bool s_is_running = false;
static bool s_is_pending_restart = false;
static uint8_t s_dot_tick = 0;

extern lv_group_t *main_group;

static void msgbox_closed_cb(bool confirm);
static void msgbox_after_close_cb(void *user_data);

static void update_terminal_text(uint32_t bytes) {
  if (s_ta_term == NULL)
    return;
  uint32_t kb = bytes / BYTES_PER_KB;
  uint32_t mb = kb / BYTES_PER_KB;
  uint32_t mb_frac = (kb % BYTES_PER_KB) * MB_FRAC_SCALE / BYTES_PER_KB;
  uint32_t packets = wifi_sniffer_get_packet_count();
  const char *state = s_is_running ? "CAPTURING" : "STOPPED";
  const char *dots[] = {"", ".", "..", "..."};
  char text[TERMINAL_TEXT_MAX];

  if (mb > 0) {
    snprintf(text,
             sizeof(text),
             "File: %s\n"
             "Size: %lu.%02lu MB\n"
             "%s%s  Pkts: %lu\n",
             s_current_filename,
             (unsigned long)mb,
             (unsigned long)mb_frac,
             state,
             dots[s_dot_tick % DOT_ANIM_COUNT],
             (unsigned long)packets);
  } else {
    snprintf(text,
             sizeof(text),
             "File: %s\n"
             "Size: %lu KB\n"
             "%s%s  Pkts: %lu\n",
             s_current_filename,
             (unsigned long)kb,
             state,
             dots[s_dot_tick % DOT_ANIM_COUNT],
             (unsigned long)packets);
  }

  lv_textarea_set_text(s_ta_term, text);
}

static void update_size_label(void) {
  uint32_t bytes = (uint32_t)wifi_sniffer_get_capture_size();
  update_terminal_text(bytes);
}

static void generate_filename(void) {
  tos_loot_generate_path(TOS_PATH_WIFI_LOOT_PCAPS,
                         "sniffer",
                         "pcap",
                         s_current_path,
                         sizeof(s_current_path),
                         s_current_filename,
                         sizeof(s_current_filename));
  update_terminal_text((uint32_t)wifi_sniffer_get_capture_size());
}

static void save_current_capture(void) {
  if (s_is_running) {
    wifi_sniffer_stop();
    s_is_running = false;
  }

  wifi_sniffer_free_buffer();
  s_is_pending_restart = true;
  msgbox_open(LV_SYMBOL_OK, "PCAP SAVED", "OK", NULL, msgbox_closed_cb);
}

static void update_timer_cb(lv_timer_t *t) {
  (void)t;
  s_dot_tick++;
  update_terminal_text((uint32_t)wifi_sniffer_get_capture_size());
}

static void restore_focus(void) {
  if (main_group != NULL && s_btn_save != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_btn_save);
    lv_group_focus_obj(s_btn_save);
    lv_group_set_editing(main_group, false);
  }
}

static void restart_capture(void) {
  generate_filename();
  wifi_sniffer_start_capture(s_current_path);
  update_size_label();
  if (wifi_sniffer_start_stream(WIFI_SNIFFER_TYPE_RAW, 0, NULL)) {
    s_is_running = true;
  } else {
    s_is_running = false;
    wifi_sniffer_stop_capture();
  }
}

static void msgbox_closed_cb(bool confirm) {
  (void)confirm;
  if (s_is_pending_restart) {
    lv_async_call(msgbox_after_close_cb, NULL);
  }
}

static void msgbox_after_close_cb(void *user_data) {
  (void)user_data;
  if (!s_is_pending_restart)
    return;
  s_is_pending_restart = false;
  restart_capture();
  restore_focus();
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_update_timer != NULL) {
      lv_timer_del(s_update_timer);
      s_update_timer = NULL;
    }
    if (s_is_running) {
      wifi_sniffer_stop();
      s_is_running = false;
    }
    wifi_sniffer_free_buffer();
    ui_switch_screen(SCREEN_WIFI_PACKETS_MENU);
  } else if (key == LV_KEY_ENTER) {
    save_current_capture();
  }
}

static void save_button_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
    save_current_capture();
  }
}

void ui_wifi_sniffer_raw_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);
  if (s_update_timer != NULL) {
    lv_timer_del(s_update_timer);
    s_update_timer = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_ta_term = lv_textarea_create(s_screen);
  lv_obj_set_size(s_ta_term, TERMINAL_W, TERMINAL_H);
  lv_obj_align(s_ta_term, LV_ALIGN_TOP_MID, 0, TERMINAL_Y);
  lv_obj_set_style_text_font(s_ta_term, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_ta_term, current_theme.screen_base, 0);
  lv_obj_set_style_text_color(s_ta_term, current_theme.border_accent, 0);
  lv_obj_set_style_radius(s_ta_term, 0, 0);
  lv_obj_set_style_border_width(s_ta_term, TERMINAL_BORDER_W, 0);
  lv_obj_set_style_border_color(s_ta_term, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_ta_term, TERMINAL_BORDER_W, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_color(s_ta_term, current_theme.border_accent, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(s_ta_term, 0, LV_STATE_FOCUS_KEY);
  lv_textarea_set_text(s_ta_term, "Listening...\n");

  s_btn_save = lv_btn_create(s_screen);
  lv_obj_set_size(s_btn_save, BTN_SAVE_W, BTN_SAVE_H);
  lv_obj_align(s_btn_save, LV_ALIGN_BOTTOM_MID, 0, BTN_SAVE_Y);
  lv_obj_set_style_bg_color(s_btn_save, current_theme.bg_item_bot, 0);
  lv_obj_set_style_bg_grad_color(s_btn_save, current_theme.bg_item_top, 0);
  lv_obj_set_style_bg_grad_dir(s_btn_save, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(s_btn_save, BTN_SAVE_BORDER_W, 0);
  lv_obj_set_style_border_color(s_btn_save, current_theme.border_inactive, 0);
  lv_obj_set_style_radius(s_btn_save, BTN_SAVE_RADIUS, 0);
  lv_obj_set_style_text_color(s_btn_save, current_theme.text_main, 0);
  lv_obj_set_style_border_color(s_btn_save, current_theme.border_accent, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_width(s_btn_save, BTN_SAVE_FOCUS_BW, LV_STATE_FOCUS_KEY);

  lv_obj_t *lbl_btn = lv_label_create(s_btn_save);
  lv_label_set_text(lbl_btn, "STOP & SAVE");
  lv_obj_center(lbl_btn);

  lv_obj_add_event_cb(s_btn_save, save_button_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_btn_save, screen_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_ta_term, screen_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  generate_filename();
  update_size_label();

  wifi_sniffer_start_capture(s_current_path);

  if (wifi_sniffer_start_stream(WIFI_SNIFFER_TYPE_RAW, 0, NULL)) {
    s_is_running = true;
  } else {
    s_is_running = false;
    wifi_sniffer_stop_capture();
  }

  s_update_timer = lv_timer_create(update_timer_cb, UPDATE_TIMER_MS, NULL);

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_ta_term);
    lv_group_add_obj(main_group, s_btn_save);
    lv_group_focus_obj(s_ta_term);
  }

  lv_screen_load(s_screen);
}

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

#include "wifi_sniffer_attack_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "msgbox_ui.h"
#include "storage_impl.h"
#include "tos_loot.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "UI_SNIFFER_ATTACK";

/* ---- Layout constants ---- */
#define LIST_W            230
#define LIST_H            160
#define LIST_Y            10
#define ITEM_H            40
#define ITEM_MARGIN_LEFT  8
#define TERMINAL_W        230
#define TERMINAL_H        125
#define TERMINAL_BORDER_W 1

/* ---- Style constants ---- */
#define STYLE_BORDER_W      2
#define STYLE_BORDER_W_ITEM 1
#define STYLE_PAD           4

/* ---- Timer / buffer sizes ---- */
#define SCAN_TICK_MS      500
#define FILENAME_MAX      64
#define FILEPATH_MAX      128
#define TERMINAL_TEXT_MAX 192
#define MSGBOX_TEXT_MAX   128

typedef enum {
  PMKID_VIEW_APS = 0,
  PMKID_VIEW_SCAN = 1,
} pmkid_view_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list_cont = NULL;
static lv_obj_t *s_loading_label = NULL;
static lv_obj_t *s_ta_term = NULL;
static lv_style_t s_style_menu;
static lv_style_t s_style_item;
static bool s_is_styles_init = false;

static pmkid_view_t s_current_view = PMKID_VIEW_APS;
static wifi_ap_record_t s_selected_ap;
static lv_timer_t *s_scan_timer = NULL;
static bool s_is_scanning = false;
static bool s_is_capture_done = false;
static char s_current_filename[FILENAME_MAX];
static char s_current_path[FILEPATH_MAX];

extern lv_group_t *main_group;

static void list_event_cb(lv_event_t *e);

static void update_scan_terminal(void) {
  if (s_ta_term == NULL)
    return;
  const char *state = s_is_capture_done ? "CAPTURED" : (s_is_scanning ? "SCANNING..." : "STOPPED");
  char text[TERMINAL_TEXT_MAX];
  snprintf(text,
           sizeof(text),
           "Target: %s  CH:%d\n"
           "File: %s\n"
           "Status: %s  Pkts:%lu\n",
           s_selected_ap.ssid,
           s_selected_ap.primary,
           s_current_filename,
           state,
           (unsigned long)wifi_sniffer_get_packet_count());
  lv_textarea_set_text(s_ta_term, text);
}

static void init_styles(void) {
  if (s_is_styles_init)
    return;

  lv_style_init(&s_style_menu);
  lv_style_set_bg_color(&s_style_menu, current_theme.screen_base);
  lv_style_set_bg_opa(&s_style_menu, LV_OPA_COVER);
  lv_style_set_border_width(&s_style_menu, STYLE_BORDER_W);
  lv_style_set_border_color(&s_style_menu, current_theme.border_interface);
  lv_style_set_radius(&s_style_menu, 0);
  lv_style_set_pad_all(&s_style_menu, STYLE_PAD);

  lv_style_init(&s_style_item);
  lv_style_set_bg_color(&s_style_item, current_theme.bg_item_bot);
  lv_style_set_bg_grad_color(&s_style_item, current_theme.bg_item_top);
  lv_style_set_bg_grad_dir(&s_style_item, LV_GRAD_DIR_VER);
  lv_style_set_border_width(&s_style_item, STYLE_BORDER_W_ITEM);
  lv_style_set_border_color(&s_style_item, current_theme.border_inactive);
  lv_style_set_radius(&s_style_item, 0);

  s_is_styles_init = true;
}

static void clear_list(void) {
  if (s_list_cont == NULL)
    return;
  uint32_t child_count = lv_obj_get_child_count(s_list_cont);
  for (uint32_t i = 0; i < child_count; i++) {
    lv_obj_del(lv_obj_get_child(s_list_cont, 0));
  }
  if (main_group != NULL)
    lv_group_remove_all_objs(main_group);
}

static void set_loading(const char *text) {
  if (s_loading_label == NULL) {
    s_loading_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_loading_label, current_theme.text_main, 0);
    lv_obj_center(s_loading_label);
  }
  lv_label_set_text(s_loading_label, text);
}

static void clear_loading(void) {
  if (s_loading_label != NULL) {
    lv_obj_del(s_loading_label);
    s_loading_label = NULL;
  }
}

static void item_focus_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *item = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_obj_set_style_border_color(item, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(item, STYLE_BORDER_W, 0);
    lv_obj_scroll_to_view(item, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(item, STYLE_BORDER_W_ITEM, 0);
  } else if (code == LV_EVENT_KEY) {
    list_event_cb(e);
  }
}

static void generate_filename(void) {
  tos_loot_generate_path(TOS_PATH_WIFI_LOOT_PCAPS,
                         "pmkid",
                         "pcap",
                         s_current_path,
                         sizeof(s_current_path),
                         s_current_filename,
                         sizeof(s_current_filename));
  update_scan_terminal();
}

static void stop_scan(void) {
  if (s_scan_timer != NULL) {
    lv_timer_del(s_scan_timer);
    s_scan_timer = NULL;
  }
  if (s_is_scanning) {
    wifi_sniffer_stop();
    s_is_scanning = false;
  }
  wifi_sniffer_free_buffer();
}

static void scan_tick_cb(lv_timer_t *t) {
  (void)t;
  update_scan_terminal();

  if (!s_is_capture_done && wifi_sniffer_pmkid_captured()) {
    wifi_sniffer_stop();
    s_is_scanning = false;
    s_is_capture_done = true;
    wifi_sniffer_free_buffer();

    char msg[MSGBOX_TEXT_MAX];
    snprintf(msg, sizeof(msg), "PMKID CAPTURED:\n%s", s_selected_ap.ssid);
    msgbox_open(LV_SYMBOL_OK, msg, "OK", NULL, NULL);
  }
}

static void show_scan_view(void) {
  clear_list();
  clear_loading();
  s_current_view = PMKID_VIEW_SCAN;
  s_is_capture_done = false;
  wifi_sniffer_clear_pmkid();

  if (s_list_cont != NULL)
    lv_obj_add_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
  if (s_ta_term != NULL)
    lv_obj_del(s_ta_term);

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
  lv_textarea_set_text(s_ta_term, "Listening...\n");

  generate_filename();
  wifi_sniffer_start_capture(s_current_path);

  if (wifi_sniffer_start_stream(WIFI_SNIFFER_TYPE_PMKID, s_selected_ap.primary, NULL)) {
    s_is_scanning = true;
  } else {
    s_is_scanning = false;
    wifi_sniffer_stop_capture();
  }

  if (s_scan_timer != NULL)
    lv_timer_del(s_scan_timer);
  s_scan_timer = lv_timer_create(scan_tick_cb, SCAN_TICK_MS, NULL);
  update_scan_terminal();

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_ta_term);
    lv_group_focus_obj(s_ta_term);
  }

  lv_obj_add_event_cb(s_ta_term, list_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, list_event_cb, LV_EVENT_KEY, NULL);
}

static void populate_ap_list(wifi_ap_record_t *results, uint16_t count) {
  if (results == NULL || count == 0) {
    lv_obj_t *empty = lv_label_create(s_list_cont);
    lv_label_set_text(empty, "NO APS FOUND");
    lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
    if (main_group != NULL)
      lv_group_add_obj(main_group, empty);
    return;
  }

  for (uint16_t i = 0; i < count; i++) {
    wifi_ap_record_t *ap = &results[i];
    lv_obj_t *item = lv_obj_create(s_list_cont);
    lv_obj_set_size(item, lv_pct(100), ITEM_H);
    lv_obj_add_style(item, &s_style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(item);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, (char *)ap->ssid);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_margin_left(lbl, ITEM_MARGIN_LEFT, 0);

    lv_obj_set_user_data(item, ap);
    lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
    if (main_group != NULL)
      lv_group_add_obj(main_group, item);
  }

  if (main_group != NULL) {
    lv_obj_t *first = lv_obj_get_child(s_list_cont, 0);
    if (first != NULL)
      lv_group_focus_obj(first);
  }
}

static void list_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_current_view == PMKID_VIEW_SCAN) {
      stop_scan();
      s_current_view = PMKID_VIEW_APS;
      if (s_ta_term != NULL) {
        lv_obj_del(s_ta_term);
        s_ta_term = NULL;
      }
      clear_list();
      if (s_list_cont != NULL)
        lv_obj_clear_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
      set_loading("SCANNING APS...");
      lv_refr_now(NULL);
      if (!wifi_service_is_active()) {
        set_loading("WIFI OFF");
        return;
      }
      wifi_service_scan();
      clear_loading();
      uint16_t count = wifi_service_get_ap_count();
      wifi_ap_record_t *results = (count > 0) ? wifi_service_get_ap_record(0) : NULL;
      populate_ap_list(results, count);
    } else {
      ui_switch_screen(SCREEN_WIFI_PACKETS_MENU);
    }
  } else if (key == LV_KEY_ENTER) {
    if (s_current_view == PMKID_VIEW_APS) {
      lv_obj_t *focused = lv_group_get_focused(main_group);
      if (focused == NULL)
        return;
      wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_obj_get_user_data(focused);
      if (ap == NULL)
        return;
      s_selected_ap = *ap;
      show_scan_view();
    }
  }
}

void ui_wifi_sniffer_attack_open(void) {
  init_styles();
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_list_cont = lv_obj_create(s_screen);
  lv_obj_set_size(s_list_cont, LIST_W, LIST_H);
  lv_obj_align(s_list_cont, LV_ALIGN_CENTER, 0, LIST_Y);
  lv_obj_add_style(s_list_cont, &s_style_menu, 0);
  lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_list_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_list_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_list_cont, LV_DIR_VER);
  lv_obj_add_event_cb(s_list_cont, list_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, list_event_cb, LV_EVENT_KEY, NULL);

  set_loading("SCANNING APS...");
  lv_screen_load(s_screen);
  lv_refr_now(NULL);

  s_current_view = PMKID_VIEW_APS;
  if (!wifi_service_is_active()) {
    set_loading("WIFI OFF");
    return;
  }
  wifi_service_scan();
  clear_loading();
  uint16_t count = wifi_service_get_ap_count();
  wifi_ap_record_t *results = (count > 0) ? wifi_service_get_ap_record(0) : NULL;
  populate_ap_list(results, count);
}

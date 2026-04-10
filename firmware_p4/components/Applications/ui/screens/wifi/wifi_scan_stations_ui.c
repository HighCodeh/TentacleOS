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

#include "wifi_scan_stations_ui.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "target_scanner.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"

static const char *TAG = "UI_SCAN_STATIONS";

/* ---- Layout constants ---- */
#define LIST_W           230
#define LIST_H           160
#define LIST_Y           10
#define ITEM_H           40
#define ITEM_MARGIN_LEFT 8
#define TITLE_OFFSET_Y   30

/* ---- Style constants ---- */
#define STYLE_BORDER_W      2
#define STYLE_BORDER_W_ITEM 1
#define STYLE_PAD           4

/* ---- Task constants ---- */
#define CLIENT_SCAN_TASK_NAME  "WifiStationsClients"
#define CLIENT_SCAN_TASK_STACK 4096
#define CLIENT_SCAN_TASK_PRIO  5
#define CLIENT_SCAN_POLL_MS    100
#define CLIENT_SCAN_TIMEOUT    700

typedef enum {
  STATIONS_VIEW_APS = 0,
  STATIONS_VIEW_CLIENTS = 1,
} stations_view_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list_cont = NULL;
static lv_obj_t *s_loading_label = NULL;
static lv_obj_t *s_empty_label = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_style_t s_style_menu;
static lv_style_t s_style_item;
static bool s_is_styles_init = false;

static stations_view_t s_current_view = STATIONS_VIEW_APS;
static wifi_ap_record_t *s_ap_results = NULL;
static uint16_t s_ap_count = 0;
static target_scanner_record_t *s_client_results = NULL;
static uint16_t s_client_count = 0;
static wifi_ap_record_t s_selected_ap;

extern lv_group_t *main_group;

static void client_scan_task(void *arg);
static void go_back_or_ap_view(void);
static void list_event_cb(lv_event_t *e);

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
  if (s_empty_label != NULL) {
    lv_obj_del(s_empty_label);
    s_empty_label = NULL;
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

static void show_ap_title(void) {
  if (s_title_label == NULL) {
    s_title_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_title_label, current_theme.text_main, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, TITLE_OFFSET_Y);
  }
  lv_label_set_text_fmt(s_title_label, "AP: %s", (char *)s_selected_ap.ssid);
  lv_obj_clear_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
}

static void hide_ap_title(void) {
  if (s_title_label != NULL) {
    lv_obj_add_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
  }
}

static void item_focus_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *item = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_obj_set_style_border_color(item, ui_theme_get_accent(), 0);
    lv_obj_set_style_border_width(item, STYLE_BORDER_W, 0);
    lv_obj_scroll_to_view(item, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(item, STYLE_BORDER_W_ITEM, 0);
  } else if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
      go_back_or_ap_view();
    } else if (key == LV_KEY_ENTER && s_current_view == STATIONS_VIEW_APS) {
      wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_obj_get_user_data(item);
      if (ap == NULL)
        return;
      s_selected_ap = *ap;
      s_current_view = STATIONS_VIEW_CLIENTS;
      clear_list();
      clear_loading();
      set_loading("SCANNING CLIENTS...");
      lv_refr_now(NULL);
      xTaskCreate(client_scan_task,
                  CLIENT_SCAN_TASK_NAME,
                  CLIENT_SCAN_TASK_STACK,
                  NULL,
                  CLIENT_SCAN_TASK_PRIO,
                  NULL);
    }
  }
}

static void populate_ap_list(wifi_ap_record_t *results, uint16_t count) {
  s_ap_results = results;
  s_ap_count = count;
  hide_ap_title();
  if (results == NULL || count == 0) {
    s_empty_label = lv_label_create(s_screen);
    lv_label_set_text(s_empty_label, "NO APS FOUND");
    lv_obj_set_style_text_color(s_empty_label, current_theme.text_main, 0);
    lv_obj_center(s_empty_label);
    lv_obj_add_event_cb(s_empty_label, list_event_cb, LV_EVENT_KEY, NULL);
    if (main_group != NULL) {
      lv_group_add_obj(main_group, s_empty_label);
      lv_group_focus_obj(s_empty_label);
    }
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

static void populate_client_list(target_scanner_record_t *results, uint16_t count) {
  s_client_results = results;
  s_client_count = count;
  show_ap_title();
  if (results == NULL || count == 0) {
    s_empty_label = lv_label_create(s_screen);
    lv_label_set_text(s_empty_label, "NO CLIENTS FOUND");
    lv_obj_set_style_text_color(s_empty_label, current_theme.text_main, 0);
    lv_obj_center(s_empty_label);
    lv_obj_add_event_cb(s_empty_label, list_event_cb, LV_EVENT_KEY, NULL);
    if (main_group != NULL) {
      lv_group_add_obj(main_group, s_empty_label);
      lv_group_focus_obj(s_empty_label);
    }
    return;
  }

  for (uint16_t i = 0; i < count; i++) {
    target_scanner_record_t *rec = &results[i];
    lv_obj_t *item = lv_obj_create(s_list_cont);
    lv_obj_set_size(item, lv_pct(100), ITEM_H);
    lv_obj_add_style(item, &s_style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(item);
    lv_label_set_text(icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text_fmt(lbl,
                          "%02X:%02X:%02X:%02X:%02X:%02X  %ddBm",
                          rec->client_mac[0],
                          rec->client_mac[1],
                          rec->client_mac[2],
                          rec->client_mac[3],
                          rec->client_mac[4],
                          rec->client_mac[5],
                          rec->rssi);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_margin_left(lbl, ITEM_MARGIN_LEFT, 0);

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

static void client_scan_task(void *arg) {
  (void)arg;
  target_scanner_start(s_selected_ap.bssid, s_selected_ap.primary);
  uint16_t count = 0;
  target_scanner_record_t *results = NULL;
  int timeout = CLIENT_SCAN_TIMEOUT;
  while (timeout-- > 0) {
    results = target_scanner_get_results(&count);
    if (results != NULL)
      break;
    vTaskDelay(pdMS_TO_TICKS(CLIENT_SCAN_POLL_MS));
  }
  if (ui_acquire()) {
    clear_loading();
    populate_client_list(results, count);
    ui_release();
  }
  vTaskDelete(NULL);
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    go_back_or_ap_view();
  }
}

static void list_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    go_back_or_ap_view();
  }
}

static void go_back_or_ap_view(void) {
  if (s_current_view == STATIONS_VIEW_CLIENTS) {
    s_current_view = STATIONS_VIEW_APS;
    clear_list();
    clear_loading();
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
    ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
  }
}

static void start_ap_scan(void) {
  s_current_view = STATIONS_VIEW_APS;
  clear_list();
  clear_loading();
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
}

void ui_wifi_scan_stations_open(void) {
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

  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  lv_screen_load(s_screen);
  lv_refr_now(NULL);

  start_ap_scan();
}

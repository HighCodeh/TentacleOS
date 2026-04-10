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

#include "wifi_scan_ap_ui.h"

#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"

static const char *TAG = "UI_WIFI_SCAN_AP";

/* ---- Layout constants ---- */
#define LIST_W            236
#define LIST_H            160
#define LIST_Y            10
#define ITEM_H            64
#define ICON_MARGIN_RIGHT 6
#define COL_PAD_GAP       2

/* ---- Style constants ---- */
#define STYLE_BORDER_W      2
#define STYLE_BORDER_W_ITEM 1
#define STYLE_PAD           4

/* ---- RSSI thresholds ---- */
#define RSSI_EXCELLENT (-50)
#define RSSI_GOOD      (-60)
#define RSSI_FAIR      (-70)
#define RSSI_WEAK      (-80)

/* ---- Populate batching ---- */
#define POPULATE_BATCH_SIZE 3
#define POPULATE_TIMER_MS   20

/* ---- Scan task ---- */
#define SCAN_TASK_NAME     "WifiScanAP"
#define SCAN_TASK_STACK    4096
#define SCAN_TASK_PRIORITY 5

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list_cont = NULL;
static lv_obj_t *s_loading_label = NULL;
static lv_style_t s_style_menu;
static lv_style_t s_style_item;
static bool s_is_styles_init = false;

extern lv_group_t *main_group;

static wifi_ap_record_t *s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static uint16_t s_populate_index = 0;
static lv_timer_t *s_populate_timer = NULL;

static const char *authmode_to_str(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI";
    default:
      return "UNKNOWN";
  }
}

static lv_opa_t rssi_opa(int8_t rssi) {
  if (rssi >= RSSI_EXCELLENT)
    return LV_OPA_COVER;
  if (rssi >= RSSI_GOOD)
    return LV_OPA_90;
  if (rssi >= RSSI_FAIR)
    return LV_OPA_70;
  if (rssi >= RSSI_WEAK)
    return LV_OPA_50;
  return LV_OPA_30;
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
      ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
    }
  }
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_populate_timer != NULL) {
      lv_timer_del(s_populate_timer);
      s_populate_timer = NULL;
    }
    if (s_scan_results != NULL) {
      free(s_scan_results);
      s_scan_results = NULL;
    }
    s_scan_count = 0;
    s_populate_index = 0;
    ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
  }
}

static void add_ap_item(const wifi_ap_record_t *ap) {
  lv_obj_t *item = lv_obj_create(s_list_cont);
  lv_obj_set_size(item, lv_pct(100), ITEM_H);
  lv_obj_add_style(item, &s_style_item, 0);
  lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *icon = lv_label_create(item);
  lv_label_set_text(icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(icon, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(icon, rssi_opa(ap->rssi), 0);
  lv_obj_set_style_margin_right(icon, ICON_MARGIN_RIGHT, 0);

  lv_obj_t *col = lv_obj_create(item);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_gap(col, COL_PAD_GAP, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *line1 = lv_label_create(col);
  lv_obj_set_width(line1, lv_pct(100));
  lv_label_set_text_fmt(line1, "%s  CH %d  %ddBm", (char *)ap->ssid, ap->primary, ap->rssi);
  lv_label_set_long_mode(line1, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(line1, current_theme.text_main, 0);

  lv_obj_t *line2 = lv_label_create(col);
  lv_obj_set_width(line2, lv_pct(100));
  lv_label_set_text_fmt(line2,
                        "MAC %02X:%02X:%02X:%02X:%02X:%02X  %s",
                        ap->bssid[0],
                        ap->bssid[1],
                        ap->bssid[2],
                        ap->bssid[3],
                        ap->bssid[4],
                        ap->bssid[5],
                        authmode_to_str(ap->authmode));
  lv_label_set_long_mode(line2, LV_LABEL_LONG_SCROLL_CIRCULAR);

  if (ap->authmode == WIFI_AUTH_OPEN) {
    lv_obj_set_style_text_color(line2, current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_text_color(line2, current_theme.border_inactive, 0);
  }

  lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
  if (main_group != NULL)
    lv_group_add_obj(main_group, item);
}

static void populate_timer_cb(lv_timer_t *t) {
  (void)t;
  if (s_list_cont == NULL)
    return;

  if (s_scan_results == NULL || s_scan_count == 0) {
    if (s_populate_timer != NULL) {
      lv_timer_del(s_populate_timer);
      s_populate_timer = NULL;
    }
    return;
  }

  uint16_t remaining = s_scan_count - s_populate_index;
  uint16_t batch = (remaining > POPULATE_BATCH_SIZE) ? POPULATE_BATCH_SIZE : remaining;
  for (uint16_t i = 0; i < batch; i++) {
    add_ap_item(&s_scan_results[s_populate_index]);
    s_populate_index++;
  }

  if (s_populate_index >= s_scan_count) {
    if (main_group != NULL) {
      lv_obj_t *first = lv_obj_get_child(s_list_cont, 0);
      if (first != NULL)
        lv_group_focus_obj(first);
    }
    if (s_populate_timer != NULL) {
      lv_timer_del(s_populate_timer);
      s_populate_timer = NULL;
    }
  }
}

static void scan_worker_task(void *arg) {
  (void)arg;
  wifi_service_scan();
  uint16_t count = wifi_service_get_ap_count();
  if (count > WIFI_SCAN_LIST_SIZE) {
    count = WIFI_SCAN_LIST_SIZE;
  }

  wifi_ap_record_t *results = NULL;
  if (count > 0) {
    results = (wifi_ap_record_t *)calloc(count, sizeof(wifi_ap_record_t));
    if (results != NULL) {
      for (uint16_t i = 0; i < count; i++) {
        wifi_ap_record_t *rec = wifi_service_get_ap_record(i);
        if (rec != NULL) {
          results[i] = *rec;
        }
      }
    } else {
      ESP_LOGE(TAG, "Failed to allocate scan results");
      count = 0;
    }
  }

  if (ui_acquire()) {
    if (s_loading_label != NULL) {
      lv_obj_del(s_loading_label);
      s_loading_label = NULL;
    }

    if (main_group != NULL)
      lv_group_remove_all_objs(main_group);
    if (s_list_cont != NULL)
      lv_obj_clean(s_list_cont);

    if (results == NULL || count == 0) {
      lv_obj_t *empty = lv_label_create(s_list_cont);
      lv_label_set_text(empty, "NO NETWORKS FOUND");
      lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
      if (main_group != NULL)
        lv_group_add_obj(main_group, empty);
      if (results != NULL)
        free(results);
    } else {
      if (s_scan_results != NULL)
        free(s_scan_results);
      s_scan_results = results;
      s_scan_count = count;
      s_populate_index = 0;
      if (s_populate_timer != NULL) {
        lv_timer_del(s_populate_timer);
        s_populate_timer = NULL;
      }
      s_populate_timer = lv_timer_create(populate_timer_cb, POPULATE_TIMER_MS, NULL);
    }
    ui_release();
  } else {
    if (results != NULL)
      free(results);
  }

  vTaskDelete(NULL);
}

void ui_wifi_scan_ap_open(void) {
  init_styles();

  if (s_screen != NULL)
    lv_obj_del(s_screen);
  if (s_populate_timer != NULL) {
    lv_timer_del(s_populate_timer);
    s_populate_timer = NULL;
  }
  if (s_scan_results != NULL) {
    free(s_scan_results);
    s_scan_results = NULL;
  }
  s_scan_count = 0;
  s_populate_index = 0;

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

  s_loading_label = lv_label_create(s_screen);
  lv_label_set_text(s_loading_label, "SCANNING...");
  lv_obj_set_style_text_color(s_loading_label, current_theme.text_main, 0);
  lv_obj_center(s_loading_label);

  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  lv_screen_load(s_screen);
  lv_refr_now(NULL);
  if (!wifi_service_is_active()) {
    lv_label_set_text(s_loading_label, "WIFI OFF");
    return;
  }
  xTaskCreate(scan_worker_task, SCAN_TASK_NAME, SCAN_TASK_STACK, NULL, SCAN_TASK_PRIORITY, NULL);
}

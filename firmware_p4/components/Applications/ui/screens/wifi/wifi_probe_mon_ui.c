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

#include "wifi_probe_mon_ui.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "menu_component_ui.h"
#include "probe_monitor.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"

static const char *SCAN_ICON = "/assets/icons/wifi_find.bin";
static const char *SSID_BROADCAST = "(broadcast)";

#define POLL_INTERVAL_MS 1000
#define PROBE_UI_MAX     20
#define PROBE_LINE_LEN   64
#define COLOR_SSID       0xB89AFF
#define TASK_STACK_SIZE  8192
#define TASK_PRIORITY    SYS_PRIO_SERVICE_LO

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static volatile bool s_poll_run = false;
static volatile bool s_worker_busy = false;

static uint16_t s_ui_count = 0;
static uint16_t s_seen_total = 0;
static uint16_t s_cued_total = 0;
static probe_monitor_record_t s_ui_rows[PROBE_UI_MAX];

static void wifi_probe_mon_input(const input_event_t *ev, void *ctx);

static void format_row(const probe_monitor_record_t *rec, char *out, size_t out_len) {
  const char *ssid = (rec->ssid[0] != '\0') ? rec->ssid : SSID_BROADCAST;
  snprintf(out,
           out_len,
           "%02X:%02X:%02X:%02X:%02X:%02X  %ddBm  %s",
           rec->mac[0],
           rec->mac[1],
           rec->mac[2],
           rec->mac[3],
           rec->mac[4],
           rec->mac[5],
           rec->rssi,
           ssid);
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  char title[24];
  snprintf(title, sizeof(title), "PROBES  %u", (unsigned)s_seen_total);
  s_menu = menu_component_create(s_screen, title, SCAN_ICON);
  if (s_ui_count == 0) {
    menu_component_add_item(&s_menu, SCAN_ICON, "Listening...");
  } else {
    char line[PROBE_LINE_LEN];
    for (int i = 0; i < (int)s_ui_count; i++) {
      format_row(&s_ui_rows[i], line, sizeof(line));
      menu_component_add_item(&s_menu, SCAN_ICON, line);
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_SSID));
    }
  }
  menu_component_set_hint(&s_menu, LV_SYMBOL_LEFT "  Back");

  ui_input_set_screen_handler(wifi_probe_mon_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void probe_refresh_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_PROBE_MON) {
    s_poll_run = false;
    return;
  }
  build_screen();
  if (s_seen_total > s_cued_total) {
    s_cued_total = s_seen_total;
    ui_feedback(UI_FB_NAV);
  }
}

static void probe_worker(void *arg) {
  (void)arg;
  wifi_service_start();
  probe_monitor_start();

  while (s_poll_run) {
    uint16_t n = 0;
    probe_monitor_record_t *res = probe_monitor_get_results(&n);
    int copied = 0;
    if (res != NULL) {
      int cap = (n < PROBE_UI_MAX) ? (int)n : PROBE_UI_MAX;
      for (int i = 0; i < cap; i++) {
        s_ui_rows[i] = res[i];
      }
      copied = cap;
    }
    probe_monitor_free_results();
    s_ui_count = (uint16_t)copied;
    s_seen_total = n;
    ui_async_call(probe_refresh_cb, NULL);
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }

  probe_monitor_stop();
  s_worker_busy = false;
  vTaskDelete(NULL);
}

static void leave_screen(void) {
  s_poll_run = false;
  ui_switch_screen(SCREEN_WIFI_MENU);
}

static void wifi_probe_mon_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        leave_screen();
      break;
    default:
      break;
  }
}

void ui_wifi_probe_mon_open(void) {
  s_ui_count = 0;
  s_seen_total = 0;
  s_cued_total = 0;

  build_screen();

  s_poll_run = true;
  if (!s_worker_busy) {
    s_worker_busy = true;
    if (xTaskCreatePinnedToCore(probe_worker,
                                "probe_mon",
                                TASK_STACK_SIZE,
                                NULL,
                                TASK_PRIORITY,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_worker_busy = false;
      s_poll_run = false;
    }
  }
}

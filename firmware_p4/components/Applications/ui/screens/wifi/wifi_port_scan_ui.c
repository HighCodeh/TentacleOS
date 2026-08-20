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

#include "wifi_port_scan_ui.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "port_scan.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"
#include "wifi_service.h"

static const char *SCAN_ICON = "/assets/icons/wifi_find.bin";

#define DEFAULT_TARGET_IP "192.168.1.1"
#define SCAN_START_PORT   1
#define SCAN_END_PORT     1024
#define COLOR_OPEN        0x00E676
#define TASK_STACK_SIZE   8192
#define TASK_PRIORITY     SYS_PRIO_SERVICE_LO
#define CAPTION_Y_OFS     78
#define WAVES_Y_OFS       -6
#define PORT_MAX          32

typedef enum { SCAN_RUNNING, SCAN_DONE } scan_state_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static scan_state_t s_state = SCAN_RUNNING;
static bool s_scanning = false;
static bool s_connected = false;
static int s_count = 0;
static port_scan_result_t s_results[PORT_MAX];

static void wifi_port_scan_input(const input_event_t *ev, void *ctx);

static const char *proto_str(port_scan_protocol_t proto) {
  return (proto == PORT_SCAN_PROTO_UDP) ? "UDP" : "TCP";
}

static const char *status_str(port_scan_status_t status) {
  return (status == PORT_SCAN_STATUS_OPEN_FILTERED) ? "OPEN|FILTERED" : "OPEN";
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

  s_menu = (menu_component_t){0};

  if (s_state == SCAN_RUNNING) {
    ui_chrome_header(s_screen, "PORT SCAN", SCAN_ICON);
    waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y_OFS, LV_SYMBOL_WIFI, SCAN_ICON);
    lv_obj_t *cap = lv_label_create(s_screen);
    lv_label_set_text(cap, "Scanning " DEFAULT_TARGET_IP);
    lv_obj_set_style_text_color(cap, current_theme.text_main, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else {
    s_menu = menu_component_create(s_screen, DEFAULT_TARGET_IP, SCAN_ICON);
    if (!s_connected) {
      menu_component_add_item(&s_menu, SCAN_ICON, "Not connected");
      menu_component_set_item_label_color(&s_menu, 0, current_theme.border_inactive);
      menu_component_set_hint(&s_menu, "Connect to Wi-Fi first");
    } else if (s_count == 0) {
      menu_component_add_item(&s_menu, SCAN_ICON, "No open ports");
      menu_component_set_hint(&s_menu, "BACK exit");
    } else {
      for (int i = 0; i < s_count; i++) {
        char row[40];
        snprintf(row, sizeof(row), "%d  %s", s_results[i].port, proto_str(s_results[i].protocol));
        menu_component_add_item(&s_menu, SCAN_ICON, row);
        menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_OPEN));
      }
      menu_component_set_hint(&s_menu, "OK details   BACK exit");
    }
  }

  ui_input_set_screen_handler(wifi_port_scan_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_PORT_SCAN)
    return;
  build_screen();
  if (s_connected && s_count > 0)
    ui_feedback(UI_FB_READ);
}

static void scan_task(void *arg) {
  (void)arg;

  int n = 0;
  bool connected = wifi_service_is_connected();
  if (connected) {
    n = port_scan_target_range(
        DEFAULT_TARGET_IP, SCAN_START_PORT, SCAN_END_PORT, s_results, PORT_MAX);
    if (n < 0)
      n = 0;
    if (n > PORT_MAX)
      n = PORT_MAX;
  }

  s_connected = connected;
  s_count = n;
  s_state = SCAN_DONE;
  s_scanning = false;
  ui_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void show_port(int idx) {
  if (idx < 0 || idx >= s_count)
    return;
  const port_scan_result_t *p = &s_results[idx];
  char msg[128];
  snprintf(msg,
           sizeof(msg),
           "%s : %d/%s\n%s\n%s",
           p->ip_str,
           p->port,
           proto_str(p->protocol),
           status_str(p->status),
           (p->banner[0] != '\0') ? p->banner : "no banner");
  msgbox_open(LV_SYMBOL_WIFI, msg, "OK", NULL, NULL);
}

static void wifi_port_scan_input(const input_event_t *ev, void *ctx) {
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
        ui_switch_screen(SCREEN_WIFI_MENU);
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press && s_state == SCAN_DONE && s_connected && s_count > 0)
        show_port(menu_component_get_selected(&s_menu));
      break;
    default:
      break;
  }
}

void ui_wifi_port_scan_open(void) {
  s_state = SCAN_RUNNING;
  s_count = 0;
  s_connected = false;
  build_screen();
  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreatePinnedToCore(
            scan_task, "port_scan", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_scanning = false;
      s_state = SCAN_DONE;
      build_screen();
    }
  }
}

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

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *SCAN_ICON = "/assets/icons/wifi_find.bin";

#define NAV_TIMER_MS    50
#define SCAN_MS         1600
#define TARGET_HOST     "192.168.1.10"
#define COLOR_OPEN      0x00E676
#define TASK_STACK_SIZE 4096
#define TASK_PRIORITY   4
#define CAPTION_Y_OFS   78
#define WAVES_Y_OFS     -6

typedef enum { SCAN_RUNNING, SCAN_DONE } scan_state_t;

typedef struct {
  int port;
  const char *proto;
  const char *service;
  const char *banner;
} open_port_t;

static const open_port_t PORT_POOL[] = {
    {22, "TCP", "SSH", "OpenSSH 8.9p1"},
    {53, "UDP", "DNS", "dnsmasq 2.85"},
    {80, "TCP", "HTTP", "nginx 1.24.0"},
    {139, "TCP", "NetBIOS", "Samba"},
    {443, "TCP", "HTTPS", "nginx 1.24.0"},
    {445, "TCP", "SMB", "Windows 10"},
    {1883, "TCP", "MQTT", "Mosquitto 2.0"},
    {8080, "TCP", "HTTP-ALT", "lighttpd"},
    {8443, "TCP", "HTTPS-ALT", "openresty"},
};
#define PORT_POOL_N ((int)(sizeof(PORT_POOL) / sizeof(PORT_POOL[0])))
#define PORT_MAX    8

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_state = SCAN_RUNNING;
static bool s_scanning = false;
static int s_count = 0;
static open_port_t s_ports[PORT_MAX];

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;

static void nav_timer_cb(lv_timer_t *t);

static void generate_ports(void) {
  bool used[PORT_POOL_N] = {false};
  int want = 3 + (int)(esp_random() % 4);
  if (want > PORT_POOL_N)
    want = PORT_POOL_N;
  int n = 0;
  for (int i = 0; i < want && n < PORT_MAX; i++) {
    int p;
    int guard = 0;
    do {
      p = (int)(esp_random() % PORT_POOL_N);
    } while (used[p] && ++guard < 32);
    if (used[p])
      continue;
    used[p] = true;
    s_ports[n++] = PORT_POOL[p];
  }
  for (int i = 1; i < n; i++) {
    open_port_t key = s_ports[i];
    int j = i - 1;
    while (j >= 0 && s_ports[j].port > key.port) {
      s_ports[j + 1] = s_ports[j];
      j--;
    }
    s_ports[j + 1] = key;
  }
  s_count = n;
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
    lv_label_set_text(cap, "Scanning " TARGET_HOST);
    lv_obj_set_style_text_color(cap, current_theme.text_main, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else {
    s_menu = menu_component_create(s_screen, TARGET_HOST, SCAN_ICON);
    if (s_count == 0) {
      menu_component_add_item(&s_menu, SCAN_ICON, "No open ports");
    } else {
      for (int i = 0; i < s_count; i++) {
        char row[40];
        snprintf(row, sizeof(row), "%d  %s", s_ports[i].port, s_ports[i].service);
        menu_component_add_item(&s_menu, SCAN_ICON, row);
        menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_OPEN));
      }
    }
    menu_component_set_hint(&s_menu, "OK banner   BACK exit");
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_PORT_SCAN)
    return;
  build_screen();
  if (s_count > 0)
    ui_feedback(UI_FB_READ);
}

static void scan_task(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(SCAN_MS));
  generate_ports();
  s_state = SCAN_DONE;
  s_scanning = false;
  lv_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void show_port(int idx) {
  if (idx < 0 || idx >= s_count)
    return;
  const open_port_t *p = &s_ports[idx];
  char msg[96];
  snprintf(msg,
           sizeof(msg),
           "%s : %d/%s\n%s\n%s",
           TARGET_HOST,
           p->port,
           p->proto,
           p->service,
           p->banner);
  msgbox_open(LV_SYMBOL_WIFI, msg, "OK", NULL, NULL);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  bool up = ui_btn_up(), down = ui_btn_down(), left = ui_btn_left();
  bool right = ui_btn_right(), ok = ok_button_is_down(), back = back_button_is_down();

  if (msgbox_is_open() || ui_input_is_locked()) {
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_right_last = right;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }
  if (down && !s_down_last)
    menu_component_next(&s_menu);
  if (up && !s_up_last)
    menu_component_prev(&s_menu);
  if ((back && !s_back_last) || (left && !s_left_last))
    ui_switch_screen(SCREEN_WIFI_MENU);
  if (((ok && !s_ok_last) || (right && !s_right_last)) && s_state == SCAN_DONE && s_count > 0)
    show_port(menu_component_get_selected(&s_menu));

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_wifi_port_scan_open(void) {
  s_state = SCAN_RUNNING;
  s_count = 0;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  build_screen();
  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreate(scan_task, "port_scan_sim", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL) !=
        pdPASS) {
      s_scanning = false;
      s_state = SCAN_DONE;
      generate_ports();
      build_screen();
    }
  }
}

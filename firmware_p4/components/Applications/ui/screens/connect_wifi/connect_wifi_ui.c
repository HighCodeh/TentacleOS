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

#include "connect_wifi_ui.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons_gpio.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "CONNECT_WIFI_UI";

#define NAV_TIMER_MS 50

#define SCAN_RESULT_COLOR_HEX 0x00E676

#define WIFI_MAX_APS     12
#define SCAN_SIM_STEPS   4
#define SCAN_SIM_STEP_MS 220

#define WIFI_TASK_STACK_SIZE 4096
#define WIFI_TASK_PRIORITY   4
#define CONNECT_SIM_STEPS    4
#define CONNECT_SIM_STEP_MS  300

#define CONNECT_STATE_FAILED    0
#define CONNECT_STATE_CONNECTED 1

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

typedef struct {
  const char *ssid;
  int8_t rssi;
} mock_ap_t;

static const mock_ap_t MOCK_APS[] = {
    {"TentacleNet", -38},
    {"HighCode-Guest", -52},
    {"Familia Souza", -60},
    {"iPhone de Ana", -67},
    {"NET_2G_A1B2", -74},
};
#define MOCK_AP_COUNT (sizeof(MOCK_APS) / sizeof(MOCK_APS[0]))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static int s_ap_count = 0;
static struct {
  char ssid[25];
  int8_t rssi;
} s_aps[WIFI_MAX_APS];

static char s_connect_ssid[33];
static char s_connect_pass[64];
static bool s_connecting = false;
static uint8_t s_connect_state = 0;
static uint8_t s_connect_ip[4] = {0};

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *t);

static const char *icon_for_rssi(int8_t rssi) {
  if (rssi >= -55)
    return "/assets/icons/wifi_icon_3.bin";
  if (rssi >= -65)
    return "/assets/icons/wifi_icon_2.bin";
  if (rssi >= -75)
    return "/assets/icons/wifi_icon_1.bin";
  return "/assets/icons/wifi_icon_0.bin";
}

static void connect_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_CONNECT_WIFI)
    return;
  if (s_connect_state == CONNECT_STATE_CONNECTED) {
    char msg[48];
    snprintf(msg,
             sizeof(msg),
             "CONNECTED\n%u.%u.%u.%u",
             s_connect_ip[0],
             s_connect_ip[1],
             s_connect_ip[2],
             s_connect_ip[3]);
    msgbox_open(LV_SYMBOL_OK, msg, "OK", NULL, NULL);
  } else {
    msgbox_open(LV_SYMBOL_CLOSE, "CONNECT FAILED\nwrong pass / range?", "OK", NULL, NULL);
  }
}

static void wifi_connect_task(void *arg) {
  (void)arg;

  for (int i = 0; i < CONNECT_SIM_STEPS; i++)
    vTaskDelay(pdMS_TO_TICKS(CONNECT_SIM_STEP_MS));

  s_connect_state = CONNECT_STATE_CONNECTED;
  s_connect_ip[0] = 192;
  s_connect_ip[1] = 168;
  s_connect_ip[2] = 1;
  s_connect_ip[3] = 42;
  s_connecting = false;
  lv_async_call(connect_done_cb, NULL);
  vTaskDelete(NULL);
}

static void on_keyboard_submit(const char *text, void *user_data) {
  (void)user_data;
  if (s_connecting || s_scanning)
    return;
  strncpy(s_connect_pass, text ? text : "", sizeof(s_connect_pass) - 1);
  s_connect_pass[sizeof(s_connect_pass) - 1] = '\0';
  s_connecting = true;
  ESP_LOGI(TAG, "connecting to '%s'...", s_connect_ssid);
  if (xTaskCreate(
          wifi_connect_task, "wifi_conn", WIFI_TASK_STACK_SIZE, NULL, WIFI_TASK_PRIORITY, NULL) !=
      pdPASS)
    s_connecting = false;
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

  s_menu = menu_component_create(s_screen, "Networks", "/assets/icons/wifi_menu_icon.bin");

  if (s_scan_state == SCAN_RUNNING) {
    menu_component_add_item(&s_menu, "/assets/icons/wifi_menu_icon.bin", "Scanning...");
  } else if (s_scan_state == SCAN_FAIL) {
    menu_component_add_item(&s_menu, "/assets/icons/wifi_menu_icon.bin", "Scan failed (C5?)");
  } else if (s_ap_count == 0) {
    menu_component_add_item(&s_menu, "/assets/icons/wifi_menu_icon.bin", "No networks found");
  } else {
    for (int i = 0; i < s_ap_count; i++) {
      menu_component_add_item(&s_menu, icon_for_rssi(s_aps[i].rssi), s_aps[i].ssid);
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(SCAN_RESULT_COLOR_HEX));
    }
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_CONNECT_WIFI)
    return;
  build_screen();
  ESP_LOGI(TAG, "scan finished: state=%d, %d AP(s)", (int)s_scan_state, s_ap_count);
}

static void wifi_scan_task(void *arg) {
  (void)arg;
  int count = 0;

  for (int i = 0; i < SCAN_SIM_STEPS; i++)
    vTaskDelay(pdMS_TO_TICKS(SCAN_SIM_STEP_MS));

  for (size_t i = 0; i < MOCK_AP_COUNT && count < WIFI_MAX_APS; i++) {
    strncpy(s_aps[count].ssid, MOCK_APS[i].ssid, sizeof(s_aps[count].ssid) - 1);
    s_aps[count].ssid[sizeof(s_aps[count].ssid) - 1] = '\0';
    s_aps[count].rssi = MOCK_APS[i].rssi;
    count++;
  }

  s_ap_count = count;
  s_scan_state = SCAN_DONE;
  s_scanning = false;
  lv_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }

  bool is_up = ui_btn_up();
  bool is_down = ui_btn_down();
  bool is_left = ui_btn_left();
  bool is_right = ui_btn_right();
  bool is_ok = ok_button_is_down();
  bool is_back = back_button_is_down();

  if (keyboard_is_open() || msgbox_is_open() || ui_input_is_locked()) {
    s_btn_up_last = is_up;
    s_btn_down_last = is_down;
    s_btn_left_last = is_left;
    s_btn_right_last = is_right;
    s_btn_ok_last = is_ok;
    s_btn_back_last = is_back;
    return;
  }

  if (is_down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (is_up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if ((is_back && !s_btn_back_last) || (is_left && !s_btn_left_last))
    ui_switch_screen(SCREEN_CONNECTION_SETTINGS);

  if ((is_ok && !s_btn_ok_last) || (is_right && !s_btn_right_last)) {
    if (s_scan_state == SCAN_DONE && s_ap_count > 0) {
      int sel = menu_component_get_selected(&s_menu);
      if (sel >= 0 && sel < s_ap_count) {
        strncpy(s_connect_ssid, s_aps[sel].ssid, sizeof(s_connect_ssid) - 1);
        s_connect_ssid[sizeof(s_connect_ssid) - 1] = '\0';
        keyboard_open(NULL, on_keyboard_submit, NULL);
      }
    }
  }

  s_btn_up_last = is_up;
  s_btn_down_last = is_down;
  s_btn_left_last = is_left;
  s_btn_right_last = is_right;
  s_btn_ok_last = is_ok;
  s_btn_back_last = is_back;
}

void ui_connect_wifi_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_ap_count = 0;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreate(
            wifi_scan_task, "wifi_scan", WIFI_TASK_STACK_SIZE, NULL, WIFI_TASK_PRIORITY, NULL) !=
        pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_FAIL;
      build_screen();
    }
  }

  ESP_LOGI(TAG, "Networks screen opened (mock scan)");
}

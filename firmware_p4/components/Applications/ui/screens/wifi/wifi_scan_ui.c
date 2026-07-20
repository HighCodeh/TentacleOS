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

#include "wifi_scan_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_names.h"

static const char *TAG_ICON = "/assets/icons/wifi_menu_icon.bin";

#define NAV_TIMER_MS    50
#define SCAN_MS         1500
#define AP_MAX          12
#define COLOR_SECURE    0x00E676
#define COLOR_OPEN      0xFFC107
#define TASK_STACK_SIZE 4096
#define TASK_PRIORITY   4

typedef enum { SCAN_RUNNING, SCAN_DONE } scan_state_t;

typedef struct {
  char ssid[25];
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  const char *enc;
} fake_ap_t;

static const char *SSID_POOL[] = {
    "NET_VIVO_2.4G",
    "VIVOFIBRA-5521",
    "CLARO_WIFI_3A",
    "GVT-A1B2",
    "TP-Link_4F2A",
    "iPhone de Ana",
    "AndroidAP_77",
    "NETVIRTUA_9988",
    "Linksys",
    "Office-Guest",
    "martin_cabo",
    "MOVISTAR_2EF1",
    "PORTAL_WIFI",
    "Familia Souza",
    "ALHN-2A40",
    "DIRECT-PC-Setup",
};
#define SSID_POOL_N ((int)(sizeof(SSID_POOL) / sizeof(SSID_POOL[0])))

static const char *ENC_POOL[] = {"WPA2", "WPA3", "WPA/WPA2", "WPA2", "OPEN"};
#define ENC_POOL_N ((int)(sizeof(ENC_POOL) / sizeof(ENC_POOL[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static bool s_scan_cued = false;
static int s_ap_count = 0;
static fake_ap_t s_aps[AP_MAX];

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

#define SRC_MAX 32

static int src_count(void) {
  int n = wifi_names_count();
  int c = n > 0 ? n : SSID_POOL_N;
  return c > SRC_MAX ? SRC_MAX : c;
}
static const char *src_ssid(int i) {
  return wifi_names_count() > 0 ? wifi_names_get(i) : SSID_POOL[i];
}

static void generate_aps(void) {
  const int pool = src_count();
  int want = 6 + (int)(esp_random() % 7);
  if (want > pool)
    want = pool;
  bool ssid_used[SRC_MAX] = {false};
  int n = 0;
  for (int i = 0; i < want && n < AP_MAX; i++) {
    int s;
    int guard = 0;
    do {
      s = (int)(esp_random() % pool);
    } while (ssid_used[s] && ++guard < 32);
    if (ssid_used[s])
      continue;
    ssid_used[s] = true;

    fake_ap_t *ap = &s_aps[n++];
    const char *name = src_ssid(s);
    strncpy(ap->ssid, name ? name : "(unknown)", sizeof(ap->ssid) - 1);
    ap->ssid[sizeof(ap->ssid) - 1] = '\0';
    for (int b = 0; b < 6; b++)
      ap->bssid[b] = (uint8_t)(esp_random() & 0xFF);
    ap->channel = (uint8_t)(1 + esp_random() % 11);
    ap->rssi = (int8_t)(-35 - (int)(esp_random() % 55));
    ap->enc = ENC_POOL[esp_random() % ENC_POOL_N];
  }

  for (int i = 1; i < n; i++) {
    fake_ap_t key = s_aps[i];
    int j = i - 1;
    while (j >= 0 && s_aps[j].rssi < key.rssi) {
      s_aps[j + 1] = s_aps[j];
      j--;
    }
    s_aps[j + 1] = key;
  }
  s_ap_count = n;
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

  s_menu = menu_component_create(s_screen, "Scan", TAG_ICON);

  if (s_scan_state == SCAN_RUNNING) {
    menu_component_add_item(&s_menu, TAG_ICON, "Scanning channels...");
  } else if (s_ap_count == 0) {
    menu_component_add_item(&s_menu, TAG_ICON, "No networks found");
  } else {
    for (int i = 0; i < s_ap_count; i++) {
      menu_component_add_item(&s_menu, icon_for_rssi(s_aps[i].rssi), s_aps[i].ssid);
      uint32_t col = (strcmp(s_aps[i].enc, "OPEN") == 0) ? COLOR_OPEN : COLOR_SECURE;
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(col));
    }
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_SCAN_MENU)
    return;
  build_screen();
  if (s_ap_count > 0 && !s_scan_cued) {
    s_scan_cued = true;
    ui_feedback(UI_FB_READ);
  }
}

static void wifi_scan_task(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(SCAN_MS));
  generate_aps();
  s_scan_state = SCAN_DONE;
  s_scanning = false;
  lv_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void show_ap_details(int idx) {
  if (idx < 0 || idx >= s_ap_count)
    return;
  const fake_ap_t *ap = &s_aps[idx];
  char msg[96];
  snprintf(msg,
           sizeof(msg),
           "%s\n%02X:%02X:%02X:%02X:%02X:%02X\nCH %d   %s\nRSSI %d dBm",
           ap->ssid,
           ap->bssid[0],
           ap->bssid[1],
           ap->bssid[2],
           ap->bssid[3],
           ap->bssid[4],
           ap->bssid[5],
           ap->channel,
           ap->enc,
           ap->rssi);
  msgbox_open(LV_SYMBOL_WIFI, msg, "OK", NULL, NULL);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (msgbox_is_open() || ui_input_is_locked()) {
    s_btn_up_last = up;
    s_btn_down_last = down;
    s_btn_left_last = left;
    s_btn_right_last = right;
    s_btn_ok_last = ok;
    s_btn_back_last = back;
    return;
  }

  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last))
    ui_switch_screen(SCREEN_WIFI_MENU);

  if (((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) && !s_scanning) {
    if (s_scan_state == SCAN_DONE && s_ap_count > 0) {
      int sel = menu_component_get_selected(&s_menu);
      if (sel >= 0 && sel < s_ap_count)
        show_ap_details(sel);
    }
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_wifi_scan_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_ap_count = 0;
  s_scan_cued = false;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreate(wifi_scan_task, "wifi_sim_scan", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL) !=
        pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_DONE;
      generate_aps();
      build_screen();
    }
  }
}

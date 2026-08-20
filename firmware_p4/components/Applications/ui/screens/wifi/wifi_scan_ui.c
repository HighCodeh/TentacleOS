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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"
#include "wifi_service.h"

static const char *TAG_ICON = "/assets/icons/wifi_find.bin";

#define AP_MAX          20
#define COLOR_SECURE    0x00E676
#define COLOR_OPEN      0xFFC107
#define TASK_STACK_SIZE 8192
#define TASK_PRIORITY   SYS_PRIO_SERVICE_LO

#define SCAN_WAVES_Y_OFS   -6
#define SCAN_CAPTION_Y_OFS 78

typedef enum { SCAN_RUNNING, SCAN_DONE } scan_state_t;

typedef struct {
  char ssid[33];
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  uint8_t authmode;
} scan_ap_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static bool s_scan_cued = false;
static int s_ap_count = 0;
static scan_ap_t s_aps[AP_MAX];

static void wifi_scan_input(const input_event_t *ev, void *ctx);

static const char *icon_for_rssi(int8_t rssi) {
  if (rssi >= -55)
    return "/assets/icons/network_wifi_3_bar.bin";
  if (rssi >= -65)
    return "/assets/icons/network_wifi_2_bar.bin";
  if (rssi >= -75)
    return "/assets/icons/network_wifi_1_bar.bin";
  return "/assets/icons/signal_wifi_0_bar.bin";
}

static const char *enc_for_authmode(uint8_t mode) {
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
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-EAP";
    default:
      return "SEC";
  }
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

  if (s_scan_state == SCAN_RUNNING) {
    ui_chrome_header(s_screen, "Scan", TAG_ICON);
    waves_create(s_screen, LV_ALIGN_CENTER, 0, SCAN_WAVES_Y_OFS, LV_SYMBOL_WIFI, TAG_ICON);
    lv_obj_t *caption = lv_label_create(s_screen);
    lv_label_set_text(caption, "Scanning...");
    lv_obj_set_style_text_color(caption, current_theme.text_main, 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, SCAN_CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else {
    s_menu = menu_component_create(s_screen, "Scan", TAG_ICON);
    if (s_ap_count == 0) {
      menu_component_add_item(&s_menu, TAG_ICON, "No networks found");
    } else {
      for (int i = 0; i < s_ap_count; i++) {
        menu_component_add_item(&s_menu, icon_for_rssi(s_aps[i].rssi), s_aps[i].ssid);
        uint32_t col = (s_aps[i].authmode == WIFI_AUTH_OPEN) ? COLOR_OPEN : COLOR_SECURE;
        menu_component_set_item_label_color(&s_menu, i, lv_color_hex(col));
      }
    }
  }

  ui_input_set_screen_handler(wifi_scan_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
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

  wifi_service_start();
  esp_err_t err = wifi_service_scan();

  int n = 0;
  if (err == ESP_OK) {
    uint16_t count = wifi_service_get_ap_count();
    for (uint16_t i = 0; i < count && n < AP_MAX; i++) {
      wifi_ap_record_t *rec = wifi_service_get_ap_record(i);
      if (rec == NULL)
        continue;
      scan_ap_t *ap = &s_aps[n++];
      strncpy(ap->ssid, (const char *)rec->ssid, sizeof(ap->ssid) - 1);
      ap->ssid[sizeof(ap->ssid) - 1] = '\0';
      memcpy(ap->bssid, rec->bssid, sizeof(ap->bssid));
      ap->channel = rec->primary;
      ap->rssi = rec->rssi;
      ap->authmode = (uint8_t)rec->authmode;
    }
  }

  s_ap_count = n;
  s_scan_state = SCAN_DONE;
  s_scanning = false;
  ui_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void show_ap_details(int idx) {
  if (idx < 0 || idx >= s_ap_count)
    return;
  const scan_ap_t *ap = &s_aps[idx];
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
           enc_for_authmode(ap->authmode),
           ap->rssi);
  msgbox_open(LV_SYMBOL_WIFI, msg, "OK", NULL, NULL);
}

static void wifi_scan_input(const input_event_t *ev, void *ctx) {
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
      if (press && !s_scanning) {
        if (s_scan_state == SCAN_DONE && s_ap_count > 0) {
          int sel = menu_component_get_selected(&s_menu);
          if (sel >= 0 && sel < s_ap_count)
            show_ap_details(sel);
        }
      }
      break;
    default:
      break;
  }
}

void ui_wifi_scan_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_ap_count = 0;
  s_scan_cued = false;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreatePinnedToCore(wifi_scan_task,
                                "wifi_scan",
                                TASK_STACK_SIZE,
                                NULL,
                                TASK_PRIORITY,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_DONE;
      s_ap_count = 0;
      build_screen();
    }
  }
}

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

#include "wifi_target_clients_ui.h"

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

#include "ap_scanner.h"
#include "target_scanner.h"
#include "wifi_service.h"

static const char *SCAN_ICON = "/assets/icons/devices.bin";
static const char *PICK_ICON = "/assets/icons/wifi_find.bin";

#define AP_PICK_MAX     MENU_COMP_MAX_ITEMS
#define CLIENT_MAX      MENU_COMP_MAX_ITEMS
#define SSID_BUF_LEN    33
#define CLIENT_POLL_MS  500
#define COLOR_CLIENT    0x00E676
#define TASK_STACK_SIZE 8192
#define TASK_PRIORITY   SYS_PRIO_SERVICE_LO
#define CAPTION_Y_OFS   78
#define WAVES_Y_OFS     -6

typedef enum { TC_PICK_SCANNING, TC_PICK_LIST, TC_CLIENTS } tc_state_t;

typedef struct {
  char ssid[SSID_BUF_LEN];
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
} pick_ap_t;

typedef struct {
  uint8_t mac[6];
  int8_t rssi;
} client_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_obj_t *s_probe_caption = NULL;

static tc_state_t s_state = TC_PICK_SCANNING;

static int s_ap_count = 0;
static pick_ap_t s_aps[AP_PICK_MAX];
static bool s_pick_scanning = false;

static uint8_t s_sel_bssid[6];
static uint8_t s_sel_channel = 0;
static char s_sel_ssid[SSID_BUF_LEN] = {0};

static client_t s_clients[CLIENT_MAX];
static int s_client_count = 0;
static bool s_client_active = false;
static bool s_showing_list = false;
static int s_shown_count = -1;

static volatile bool s_poll_run = false;
static volatile bool s_worker_busy = false;
static volatile bool s_restart_pending = false;

static void wifi_target_clients_input(const input_event_t *ev, void *ctx);
static void build_screen(void);

static void ap_row_text(const pick_ap_t *ap, char *dst, size_t n) {
  if (ap->ssid[0] != '\0')
    snprintf(dst, n, "%s", ap->ssid);
  else
    snprintf(dst, n, "%02X:%02X:%02X CH%d", ap->bssid[3], ap->bssid[4], ap->bssid[5], ap->channel);
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_menu = (menu_component_t){0};
  s_probe_caption = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  if (s_state == TC_PICK_SCANNING) {
    ui_chrome_header(s_screen, "TARGET", PICK_ICON);
    waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y_OFS, LV_SYMBOL_WIFI, PICK_ICON);
    lv_obj_t *cap = lv_label_create(s_screen);
    lv_label_set_text(cap, "Scanning...");
    lv_obj_set_style_text_color(cap, current_theme.text_main, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else if (s_state == TC_PICK_LIST) {
    s_menu = menu_component_create(s_screen, "TARGET", PICK_ICON);
    if (s_ap_count == 0) {
      menu_component_add_item(&s_menu, PICK_ICON, "No networks found");
    } else {
      for (int i = 0; i < s_ap_count; i++) {
        char row[SSID_BUF_LEN + 16];
        ap_row_text(&s_aps[i], row, sizeof(row));
        menu_component_add_item(&s_menu, PICK_ICON, row);
      }
    }
    menu_component_set_hint(&s_menu, "OK target   BACK exit");
  } else if (!s_showing_list) {
    ui_chrome_header(s_screen, "CLIENTS", SCAN_ICON);
    waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y_OFS, LV_SYMBOL_WIFI, SCAN_ICON);
    s_probe_caption = lv_label_create(s_screen);
    lv_label_set_text_fmt(s_probe_caption, "Probing %s", s_sel_ssid);
    lv_obj_set_style_text_color(s_probe_caption, current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_probe_caption, &lv_font_montserrat_14, 0);
    lv_obj_align(s_probe_caption, LV_ALIGN_CENTER, 0, CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else {
    char title[SSID_BUF_LEN + 8];
    snprintf(title, sizeof(title), "%s CH%d", s_sel_ssid, s_sel_channel);
    s_menu = menu_component_create(s_screen, title, SCAN_ICON);
    for (int i = 0; i < s_client_count; i++) {
      char row[40];
      snprintf(row,
               sizeof(row),
               "%02X:%02X:%02X   %d dBm",
               s_clients[i].mac[3],
               s_clients[i].mac[4],
               s_clients[i].mac[5],
               s_clients[i].rssi);
      menu_component_add_item(&s_menu, SCAN_ICON, row);
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_CLIENT));
    }
    menu_component_set_hint(&s_menu, "OK detail   BACK exit");
  }

  ui_input_set_screen_handler(wifi_target_clients_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void client_update_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_TARGET_CLIENTS || s_state != TC_CLIENTS) {
    s_poll_run = false;
    return;
  }

  if (s_client_count == 0) {
    if (!s_showing_list && s_probe_caption != NULL) {
      if (s_client_active)
        lv_label_set_text_fmt(s_probe_caption, "Probing %s", s_sel_ssid);
      else
        lv_label_set_text(s_probe_caption, "No clients found");
    } else {
      s_showing_list = false;
      build_screen();
    }
    return;
  }

  if (!s_showing_list || s_shown_count != s_client_count) {
    s_showing_list = true;
    s_shown_count = s_client_count;
    build_screen();
    ui_feedback(UI_FB_READ);
  }
}

static void client_worker(void *arg) {
  (void)arg;
  for (;;) {
    s_restart_pending = false;
    uint8_t bssid[6];
    uint8_t channel;
    memcpy(bssid, s_sel_bssid, sizeof(bssid));
    channel = s_sel_channel;

    bool started = target_scanner_start(bssid, channel);
    s_client_count = 0;
    s_client_active = started;
    ui_async_call(client_update_cb, NULL);

    bool scanning = started;
    while (started && s_poll_run && !s_restart_pending && scanning) {
      vTaskDelay(pdMS_TO_TICKS(CLIENT_POLL_MS));
      if (!s_poll_run || s_restart_pending)
        break;
      uint16_t cnt = 0;
      bool is_scanning = false;
      target_scanner_record_t *recs = target_scanner_get_live_results(&cnt, &is_scanning);
      int n = 0;
      if (recs != NULL) {
        for (uint16_t i = 0; i < cnt && n < CLIENT_MAX; i++) {
          memcpy(s_clients[n].mac, recs[i].client_mac, sizeof(s_clients[n].mac));
          s_clients[n].rssi = recs[i].rssi;
          n++;
        }
      }
      s_client_count = n;
      scanning = is_scanning;
      s_client_active = is_scanning;
      ui_async_call(client_update_cb, NULL);
    }

    target_scanner_free_results();
    if (!s_restart_pending)
      break;
  }
  s_worker_busy = false;
  vTaskDelete(NULL);
}

static void start_client_scan(void) {
  s_state = TC_CLIENTS;
  s_client_count = 0;
  s_client_active = true;
  s_showing_list = false;
  s_shown_count = -1;
  build_screen();

  s_poll_run = true;
  if (!s_worker_busy) {
    s_worker_busy = true;
    s_restart_pending = false;
    if (xTaskCreatePinnedToCore(client_worker,
                                "tgt_scan",
                                TASK_STACK_SIZE,
                                NULL,
                                TASK_PRIORITY,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_worker_busy = false;
      s_client_active = false;
      msgbox_open(SCAN_ICON, "Scan start failed (C5?)", "OK", NULL, NULL);
      ui_input_set_screen_handler(wifi_target_clients_input, NULL);
    }
  } else {
    s_restart_pending = true;
  }
}

static void ap_pick_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_TARGET_CLIENTS || s_state != TC_PICK_SCANNING)
    return;
  s_state = TC_PICK_LIST;
  build_screen();
  if (s_ap_count > 0)
    ui_feedback(UI_FB_READ);
}

static void ap_pick_task(void *arg) {
  (void)arg;
  int n = 0;

  wifi_service_start();
  if (ap_scanner_start()) {
    uint16_t count = 0;
    wifi_ap_record_t *recs = ap_scanner_get_results(&count);
    if (recs != NULL) {
      for (uint16_t i = 0; i < count && n < AP_PICK_MAX; i++) {
        pick_ap_t *ap = &s_aps[n++];
        strncpy(ap->ssid, (const char *)recs[i].ssid, sizeof(ap->ssid) - 1);
        ap->ssid[sizeof(ap->ssid) - 1] = '\0';
        memcpy(ap->bssid, recs[i].bssid, sizeof(ap->bssid));
        ap->channel = recs[i].primary;
        ap->rssi = recs[i].rssi;
      }
    }
    ap_scanner_free_results();
  }

  s_ap_count = n;
  s_pick_scanning = false;
  ui_async_call(ap_pick_done_cb, NULL);
  vTaskDelete(NULL);
}

static void select_target(int idx) {
  if (idx < 0 || idx >= s_ap_count)
    return;
  const pick_ap_t *ap = &s_aps[idx];
  memcpy(s_sel_bssid, ap->bssid, sizeof(s_sel_bssid));
  s_sel_channel = ap->channel;
  strncpy(s_sel_ssid, ap->ssid, sizeof(s_sel_ssid) - 1);
  s_sel_ssid[sizeof(s_sel_ssid) - 1] = '\0';
  start_client_scan();
}

static void show_client(int idx) {
  if (idx < 0 || idx >= s_client_count)
    return;
  const client_t *c = &s_clients[idx];
  char msg[112];
  snprintf(msg,
           sizeof(msg),
           "%02X:%02X:%02X:%02X:%02X:%02X\nRSSI %d dBm\nassoc %s",
           c->mac[0],
           c->mac[1],
           c->mac[2],
           c->mac[3],
           c->mac[4],
           c->mac[5],
           c->rssi,
           s_sel_ssid);
  msgbox_open(LV_SYMBOL_WIFI, msg, "OK", NULL, NULL);
}

static void leave_screen(void) {
  s_poll_run = false;
  ui_switch_screen(SCREEN_WIFI_MENU);
}

static void wifi_target_clients_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_state) {
    case TC_PICK_SCANNING:
      if ((ev->button == INPUT_BTN_BACK || ev->button == INPUT_BTN_LEFT) && press)
        ui_switch_screen(SCREEN_WIFI_MENU);
      break;

    case TC_PICK_LIST:
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
          if (press && s_ap_count > 0)
            select_target(menu_component_get_selected(&s_menu));
          break;
        default:
          break;
      }
      break;

    case TC_CLIENTS:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_showing_list)
            menu_component_next(&s_menu);
          break;
        case INPUT_BTN_UP:
          if (nav && s_showing_list)
            menu_component_prev(&s_menu);
          break;
        case INPUT_BTN_BACK:
        case INPUT_BTN_LEFT:
          if (press)
            leave_screen();
          break;
        case INPUT_BTN_OK:
        case INPUT_BTN_RIGHT:
          if (press && s_showing_list)
            show_client(menu_component_get_selected(&s_menu));
          break;
        default:
          break;
      }
      break;

    default:
      break;
  }
}

void ui_wifi_target_clients_open(void) {
  s_poll_run = false;
  s_state = TC_PICK_SCANNING;
  s_ap_count = 0;
  s_client_count = 0;
  s_showing_list = false;
  s_shown_count = -1;
  build_screen();

  if (!s_pick_scanning) {
    s_pick_scanning = true;
    if (xTaskCreatePinnedToCore(
            ap_pick_task, "tgt_pick", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_pick_scanning = false;
      s_state = TC_PICK_LIST;
      build_screen();
    }
  }
}

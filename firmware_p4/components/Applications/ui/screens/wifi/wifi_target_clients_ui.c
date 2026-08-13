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

static const char *SCAN_ICON = "/assets/icons/devices.bin";

#define NAV_TIMER_MS    50
#define SCAN_MS         1400
#define TARGET_AP       "HOME-5G  CH6"
#define COLOR_CLIENT    0x00E676
#define TASK_STACK_SIZE 4096
#define TASK_PRIORITY   4
#define CLIENT_MAX      8
#define CAPTION_Y_OFS   78
#define WAVES_Y_OFS     -6

typedef enum { SCAN_RUNNING, SCAN_DONE } scan_state_t;

typedef struct {
  uint8_t mac[6];
  int8_t rssi;
} client_t;

static const char *VENDOR_POOL[] = {
    "Apple", "Samsung", "Intel", "Espressif", "Xiaomi", "Randomized"};
#define VENDOR_POOL_N ((int)(sizeof(VENDOR_POOL) / sizeof(VENDOR_POOL[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_state = SCAN_RUNNING;
static bool s_scanning = false;
static int s_count = 0;
static client_t s_clients[CLIENT_MAX];
static const char *s_vendor[CLIENT_MAX];

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;

static void nav_timer_cb(lv_timer_t *t);

static void generate_clients(void) {
  int want = 2 + (int)(esp_random() % 4);
  if (want > CLIENT_MAX)
    want = CLIENT_MAX;
  for (int i = 0; i < want; i++) {
    for (int b = 0; b < 6; b++)
      s_clients[i].mac[b] = (uint8_t)(esp_random() & 0xFF);
    s_clients[i].rssi = (int8_t)(-38 - (int)(esp_random() % 50));
    s_vendor[i] = VENDOR_POOL[esp_random() % VENDOR_POOL_N];
  }
  for (int i = 1; i < want; i++) {
    client_t kc = s_clients[i];
    const char *kv = s_vendor[i];
    int j = i - 1;
    while (j >= 0 && s_clients[j].rssi < kc.rssi) {
      s_clients[j + 1] = s_clients[j];
      s_vendor[j + 1] = s_vendor[j];
      j--;
    }
    s_clients[j + 1] = kc;
    s_vendor[j + 1] = kv;
  }
  s_count = want;
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
    ui_chrome_header(s_screen, "CLIENTS", SCAN_ICON);
    waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y_OFS, LV_SYMBOL_WIFI, SCAN_ICON);
    lv_obj_t *cap = lv_label_create(s_screen);
    lv_label_set_text(cap, "Probing " TARGET_AP);
    lv_obj_set_style_text_color(cap, current_theme.text_main, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else {
    s_menu = menu_component_create(s_screen, TARGET_AP, SCAN_ICON);
    if (s_count == 0) {
      menu_component_add_item(&s_menu, SCAN_ICON, "No clients");
    } else {
      for (int i = 0; i < s_count; i++) {
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
    }
    menu_component_set_hint(&s_menu, "OK detail   BACK exit");
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_TARGET_CLIENTS)
    return;
  build_screen();
  if (s_count > 0)
    ui_feedback(UI_FB_READ);
}

static void scan_task(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(SCAN_MS));
  generate_clients();
  s_state = SCAN_DONE;
  s_scanning = false;
  lv_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void show_client(int idx) {
  if (idx < 0 || idx >= s_count)
    return;
  const client_t *c = &s_clients[idx];
  char msg[112];
  snprintf(msg,
           sizeof(msg),
           "%02X:%02X:%02X:%02X:%02X:%02X\n%s\nRSSI %d dBm\nassoc %s",
           c->mac[0],
           c->mac[1],
           c->mac[2],
           c->mac[3],
           c->mac[4],
           c->mac[5],
           s_vendor[idx],
           c->rssi,
           TARGET_AP);
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
    show_client(menu_component_get_selected(&s_menu));

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_wifi_target_clients_open(void) {
  s_state = SCAN_RUNNING;
  s_count = 0;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  build_screen();
  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreate(scan_task, "tgt_clients_sim", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL) !=
        pdPASS) {
      s_scanning = false;
      s_state = SCAN_DONE;
      generate_clients();
      build_screen();
    }
  }
}

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

#include "wifi_channel_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bridge.h"
#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "WIFI_CHAN_UI";

#define NAV_TIMER_MS       50
#define CHAN_ICON          "/assets/icons/wifi_menu_icon.bin"
#define MAX_CHANNELS       14
#define MAX_ROWS           12
#define SCAN_SETTLE_MS     150
#define SCAN_POLL_TRIES    30
#define SCAN_POLL_DELAY_MS 400
#define TASK_STACK_SIZE    4096
#define TASK_PRIORITY      4

#define COLOR_QUIET_HEX   0x00E676
#define COLOR_BUSY_HEX    0xFFC107
#define COLOR_CROWDED_HEX 0xF44336

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static int s_row_count = 0;
static char s_rows[MAX_ROWS][32];
static uint32_t s_row_color[MAX_ROWS];

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *t);

static uint32_t color_for_count(int count) {
  if (count <= 2)
    return COLOR_QUIET_HEX;
  if (count <= 4)
    return COLOR_BUSY_HEX;
  return COLOR_CROWDED_HEX;
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

  s_menu = menu_component_create(s_screen, "Channels", CHAN_ICON);

  if (s_scan_state == SCAN_RUNNING) {
    menu_component_add_item(&s_menu, CHAN_ICON, "Scanning...");
  } else if (s_scan_state == SCAN_FAIL) {
    menu_component_add_item(&s_menu, CHAN_ICON, "Scan failed (C5?)");
  } else if (s_row_count == 0) {
    menu_component_add_item(&s_menu, CHAN_ICON, "No networks found");
  } else {
    for (int i = 0; i < s_row_count; i++) {
      menu_component_add_item(&s_menu, CHAN_ICON, s_rows[i]);
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(s_row_color[i]));
    }
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_CHANNELS)
    return;
  build_screen();
  ESP_LOGI(
      TAG, "channel analysis: state=%d, %d occupied channel(s)", (int)s_scan_state, s_row_count);
}

static void wifi_channel_task(void *arg) {
  (void)arg;
  scan_state_t result = SCAN_FAIL;
  int rows = 0;

  int ch_count[MAX_CHANNELS + 1] = {0};
  int8_t ch_best[MAX_CHANNELS + 1];
  for (int i = 0; i <= MAX_CHANNELS; i++)
    ch_best[i] = -128;

  if (bridge_master_init() == ESP_OK) {
    bridge_frame_t req = {.cmd = BRIDGE_CMD_WIFI_SCAN_START};
    bridge_frame_t resp = {0};
    if (bridge_request(&req, &resp, SCAN_SETTLE_MS) == ESP_OK && resp.status == BRIDGE_STATUS_OK) {
      uint8_t n = 0;
      for (int i = 0; i < SCAN_POLL_TRIES; i++) {
        vTaskDelay(pdMS_TO_TICKS(SCAN_POLL_DELAY_MS));
        req.cmd = BRIDGE_CMD_WIFI_SCAN_COUNT;
        if (bridge_request(&req, &resp, SCAN_SETTLE_MS) == ESP_OK &&
            resp.status == BRIDGE_STATUS_OK && resp.payload[0] > 0) {
          n = resp.payload[0];
          break;
        }
      }
      result = SCAN_DONE;

      for (int i = 0; i < n; i++) {
        req.cmd = BRIDGE_CMD_WIFI_SCAN_GET;
        req.len = 1;
        req.payload[0] = (uint8_t)i;
        if (bridge_request(&req, &resp, SCAN_SETTLE_MS) == ESP_OK &&
            resp.status == BRIDGE_STATUS_OK) {
          bridge_wifi_ap_t ap;
          memcpy(&ap, resp.payload, sizeof(ap));
          if (!ap.valid)
            continue;
          int ch = ap.channel;
          if (ch < 1 || ch > MAX_CHANNELS)
            continue;
          ch_count[ch]++;
          int8_t rssi = (int8_t)ap.rssi;
          if (rssi > ch_best[ch])
            ch_best[ch] = rssi;
        }
      }

      for (int ch = 1; ch <= MAX_CHANNELS && rows < MAX_ROWS; ch++) {
        if (ch_count[ch] == 0)
          continue;
        snprintf(s_rows[rows],
                 sizeof(s_rows[rows]),
                 "Ch%2d   %d AP   %d dBm",
                 ch,
                 ch_count[ch],
                 ch_best[ch]);
        s_row_color[rows] = color_for_count(ch_count[ch]);
        rows++;
      }
    } else {
      ESP_LOGE(TAG, "WIFI_SCAN_START failed (bridge/C5 not responding)");
    }
  } else {
    ESP_LOGE(TAG, "bridge_master_init failed");
  }

  s_row_count = rows;
  s_scan_state = result;
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

  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last))
    ui_switch_screen(SCREEN_WIFI_MENU);

  if (((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) && !s_scanning) {
    ui_wifi_channel_open();
    return;
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_wifi_channel_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_row_count = 0;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreate(wifi_channel_task, "wifi_chan", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL) !=
        pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_FAIL;
      build_screen();
    }
  }

  ESP_LOGI(TAG, "Channel analysis screen opened — real C5 scan started");
}

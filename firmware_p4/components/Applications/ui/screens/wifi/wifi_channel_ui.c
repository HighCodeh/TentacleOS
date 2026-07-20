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

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "WIFI_CHAN_UI";

#define NAV_TIMER_MS     50
#define CHAN_ICON        "/assets/icons/wifi_menu_icon.bin"
#define MAX_ROWS         12
#define SCAN_SIM_STEPS   4
#define SCAN_SIM_STEP_MS 220
#define TASK_STACK_SIZE  4096
#define TASK_PRIORITY    4

#define COLOR_QUIET_HEX   0x00E676
#define COLOR_BUSY_HEX    0xFFC107
#define COLOR_CROWDED_HEX 0xF44336

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

typedef struct {
  uint8_t channel;
  uint8_t ap_count;
  int8_t best_rssi;
} mock_channel_t;

static const mock_channel_t MOCK_CHANNELS[] = {
    {1, 5, -42},
    {6, 3, -55},
    {11, 4, -49},
    {3, 1, -71},
    {9, 2, -63},
};
#define MOCK_CHANNEL_COUNT (sizeof(MOCK_CHANNELS) / sizeof(MOCK_CHANNELS[0]))

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
  int rows = 0;

  for (int i = 0; i < SCAN_SIM_STEPS; i++)
    vTaskDelay(pdMS_TO_TICKS(SCAN_SIM_STEP_MS));

  for (size_t i = 0; i < MOCK_CHANNEL_COUNT && rows < MAX_ROWS; i++) {
    snprintf(s_rows[rows],
             sizeof(s_rows[rows]),
             "Ch%2d   %d AP   %d dBm",
             MOCK_CHANNELS[i].channel,
             MOCK_CHANNELS[i].ap_count,
             MOCK_CHANNELS[i].best_rssi);
    s_row_color[rows] = color_for_count(MOCK_CHANNELS[i].ap_count);
    rows++;
  }

  s_row_count = rows;
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

  ESP_LOGI(TAG, "Channel analysis screen opened (mock scan)");
}

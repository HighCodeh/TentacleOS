// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wifi_scan_ui.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "ap_scanner.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_SCAN";

#define SPINNER_SIZE      80
#define SPINNER_OFFSET_Y  (-20)
#define ARC_WIDTH         4
#define STATUS_OFFSET_Y   40
#define ICON_OFFSET_Y     (-30)
#define SCAN_POLL_MS      100
#define SCAN_TIMEOUT      100
#define RESULT_DISPLAY_MS 1000
#define SCAN_TASK_NAME    "WifiScanWorker"
#define SCAN_TASK_STACK   4096
#define SCAN_TASK_PRIO    5

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_spinner = NULL;
static lv_obj_t *s_lbl_status = NULL;

static void scan_worker_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "Starting AP Scanner...");

  ap_scanner_start();

  uint16_t count = 0;
  int timeout = SCAN_TIMEOUT;
  while (ap_scanner_get_results(&count) == NULL && timeout > 0) {
    vTaskDelay(pdMS_TO_TICKS(SCAN_POLL_MS));
    timeout--;
  }

  if (ui_acquire()) {
    if (s_spinner != NULL) {
      lv_obj_del(s_spinner);
      s_spinner = NULL;
    }

    if (s_lbl_status != NULL) {
      lv_label_set_text_fmt(s_lbl_status, "Found %d Networks!", count);
      lv_obj_set_style_text_color(s_lbl_status, current_theme.text_main, 0);
      lv_obj_align(s_lbl_status, LV_ALIGN_CENTER, 0, 0);

      lv_obj_t *icon = lv_label_create(s_screen);
      lv_label_set_text(icon, LV_SYMBOL_OK);
      lv_obj_set_style_text_color(icon, current_theme.text_main, 0);
      lv_obj_align(icon, LV_ALIGN_CENTER, 0, ICON_OFFSET_Y);
    }

    ui_release();
  }

  vTaskDelay(pdMS_TO_TICKS(RESULT_DISPLAY_MS));
  ui_switch_screen(SCREEN_WIFI_AP_LIST);

  vTaskDelete(NULL);
}

void ui_wifi_scan_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);

  s_spinner = lv_spinner_create(s_screen);
  lv_obj_set_size(s_spinner, SPINNER_SIZE, SPINNER_SIZE);
  lv_obj_align(s_spinner, LV_ALIGN_CENTER, 0, SPINNER_OFFSET_Y);

  lv_obj_set_style_arc_color(s_spinner, ui_theme_get_accent(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_spinner, current_theme.border_inactive, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_spinner, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_spinner, ARC_WIDTH, LV_PART_INDICATOR);

  s_lbl_status = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_status, "Scanning...");
  lv_obj_set_style_text_color(s_lbl_status, current_theme.text_main, 0);
  lv_obj_align(s_lbl_status, LV_ALIGN_CENTER, 0, STATUS_OFFSET_Y);

  lv_screen_load(s_screen);

  xTaskCreate(scan_worker_task, SCAN_TASK_NAME, SCAN_TASK_STACK, NULL, SCAN_TASK_PRIO, NULL);
}

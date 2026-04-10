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

#include "ui_badusb_connect.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bad_usb.h"
#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_BADUSB_CONNECT";

#define SPINNER_SIZE          50
#define STATUS_LABEL_OFFSET_Y 50
#define HINT_LABEL_OFFSET_Y   70
#define WAITER_TASK_STACK     4096
#define WAITER_TASK_PRIORITY  5

static lv_obj_t *s_screen_connect = NULL;
static lv_obj_t *s_spinner = NULL;
static TaskHandle_t s_waiter_task = NULL;

static void connection_waiter_task(void *pvParameters);
static void connect_key_event_cb(lv_event_t *e);

void ui_badusb_connect_open(void) {
  if (s_screen_connect != NULL) {
    lv_obj_del(s_screen_connect);
  }

  s_screen_connect = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_connect, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen_connect, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen_connect);

  s_spinner = lv_spinner_create(s_screen_connect);
  lv_obj_set_size(s_spinner, SPINNER_SIZE, SPINNER_SIZE);
  lv_obj_center(s_spinner);
  lv_obj_set_style_arc_color(s_spinner, lv_palette_main(LV_PALETTE_DEEP_PURPLE), LV_PART_INDICATOR);

  lv_obj_t *lbl_status = lv_label_create(s_screen_connect);
  lv_label_set_text(lbl_status, "Waiting for USB...");
  lv_obj_set_style_text_color(lbl_status, current_theme.text_main, 0);
  lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, STATUS_LABEL_OFFSET_Y);

  lv_obj_t *lbl_hint = lv_label_create(s_screen_connect);
  lv_label_set_text(lbl_hint, "Connect to PC now");
  lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl_hint, current_theme.text_main, 0);
  lv_obj_align(lbl_hint, LV_ALIGN_CENTER, 0, HINT_LABEL_OFFSET_Y);

  footer_ui_create(s_screen_connect);

  lv_obj_add_event_cb(s_screen_connect, connect_key_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen_connect);
    lv_group_focus_obj(s_screen_connect);
  }

  lv_screen_load(s_screen_connect);

  xTaskCreate(connection_waiter_task,
              "usb_waiter",
              WAITER_TASK_STACK,
              NULL,
              WAITER_TASK_PRIORITY,
              &s_waiter_task);
}

static void connection_waiter_task(void *pvParameters) {
  bad_usb_wait_for_connection();
  ui_switch_screen(SCREEN_BADUSB_RUNNING);
  s_waiter_task = NULL;
  vTaskDelete(NULL);
}

static void connect_key_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
      if (s_waiter_task != NULL) {
        vTaskDelete(s_waiter_task);
        s_waiter_task = NULL;
      }
      bad_usb_deinit();
      ui_switch_screen(SCREEN_BADUSB_BROWSER);
    }
  }
}
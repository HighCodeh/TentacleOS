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

#include "ui_badusb_running.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bad_usb.h"
#include "ducky_parser.h"
#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_BADUSB_RUNNING";

#define SCRIPT_NAME_MAX_LEN         64
#define SCRIPT_FULL_PATH_MAX_LEN    128
#define SCRIPT_DISPLAY_NAME_MAX_LEN 56
#define SCRIPT_PATH_PREFIX          "storage/bad_usb_scripts/"
#define SCRIPT_TASK_STACK_SIZE      4096
#define SCRIPT_TASK_PRIORITY        5
#define TITLE_LABEL_OFFSET_Y        (-40)
#define INFO_LABEL_OFFSET_Y         40
#define PROGRESS_BAR_WIDTH          200
#define PROGRESS_BAR_HEIGHT         20
#define PROGRESS_BAR_BORDER_WIDTH   1
#define PROGRESS_PERCENT_MAX        100

static lv_obj_t *s_screen_running = NULL;
static lv_obj_t *s_progress_bar = NULL;
static TaskHandle_t s_script_task_handle = NULL;
static char s_script_name[SCRIPT_NAME_MAX_LEN] = "rickroll.txt";

static void ducky_progress_cb(int current_line, int total_lines);
static void script_runner_task(void *pvParameters);
static void running_key_event_cb(lv_event_t *e);

void ui_badusb_running_set_script(const char *name) {
  if (name != NULL) {
    strncpy(s_script_name, name, sizeof(s_script_name) - 1);
    s_script_name[sizeof(s_script_name) - 1] = '\0';
  }
}

void ui_badusb_running_open(void) {
  if (s_screen_running != NULL) {
    lv_obj_del(s_screen_running);
  }

  s_screen_running = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_running, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen_running, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen_running);
  footer_ui_create(s_screen_running);

  char display_name[SCRIPT_DISPLAY_NAME_MAX_LEN];
  char *dot = strrchr(s_script_name, '.');
  if (dot != NULL) {
    size_t len = dot - s_script_name;
    if (len > sizeof(display_name) - 1) {
      len = sizeof(display_name) - 1;
    }
    strncpy(display_name, s_script_name, len);
    display_name[len] = '\0';
  } else {
    strncpy(display_name, s_script_name, sizeof(display_name) - 1);
    display_name[sizeof(display_name) - 1] = '\0';
  }

  lv_obj_t *lbl_title = lv_label_create(s_screen_running);
  lv_label_set_text_fmt(lbl_title, "Running: %s", display_name);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, TITLE_LABEL_OFFSET_Y);

  s_progress_bar = lv_bar_create(s_screen_running);
  lv_obj_set_size(s_progress_bar, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT);
  lv_obj_center(s_progress_bar);
  lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(s_progress_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_progress_bar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_border_width(s_progress_bar, PROGRESS_BAR_BORDER_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_border_color(
      s_progress_bar, lv_palette_main(LV_PALETTE_DEEP_PURPLE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      s_progress_bar, lv_palette_main(LV_PALETTE_DEEP_PURPLE), LV_PART_INDICATOR);

  lv_obj_t *lbl_info = lv_label_create(s_screen_running);
  lv_label_set_text(lbl_info, "Press BACK to cancel");
  lv_obj_align(lbl_info, LV_ALIGN_CENTER, 0, INFO_LABEL_OFFSET_Y);

  lv_obj_add_event_cb(s_screen_running, running_key_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen_running);
    lv_group_focus_obj(s_screen_running);
  }

  lv_screen_load(s_screen_running);

  xTaskCreate(script_runner_task,
              "script_runner",
              SCRIPT_TASK_STACK_SIZE,
              NULL,
              SCRIPT_TASK_PRIORITY,
              &s_script_task_handle);
}

static void ducky_progress_cb(int current_line, int total_lines) {
  if (s_progress_bar != NULL && ui_acquire()) {
    int progress = (current_line * PROGRESS_PERCENT_MAX) / total_lines;
    lv_bar_set_value(s_progress_bar, progress, LV_ANIM_OFF);
    ui_release();
  }
}

static void script_runner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting script: %s", s_script_name);

  char full_path[SCRIPT_FULL_PATH_MAX_LEN];
  snprintf(full_path, sizeof(full_path), "%s%s", SCRIPT_PATH_PREFIX, s_script_name);

  ducky_set_progress_callback(ducky_progress_cb);
  ducky_run_from_assets(full_path);
  ducky_set_progress_callback(NULL);

  bad_usb_deinit();
  ui_switch_screen(SCREEN_BADUSB_BROWSER);
  s_script_task_handle = NULL;
  vTaskDelete(NULL);
}

static void running_key_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC) {
      if (s_script_task_handle != NULL) {
        ducky_abort();
      }
    }
  }
}
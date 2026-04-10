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

#include "ir_burst_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ir.h"
#include "ir_file.h"
#include "msgbox_ui.h"
#include "spinner_ui.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_BURST_UI";

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2
#define STATUS_LABEL_OFFSET_Y  (-10)
#define COUNT_LABEL_OFFSET_Y   15
#define SPINNER_OFFSET_Y       (-20)
#define SPINNER_SIZE           30
#define NAV_TIMER_INTERVAL_MS  50
#define BURST_DELAY_MS         150
#define BURST_TASK_STACK_SIZE  8192
#define BURST_TASK_PRIORITY    5
#define BURST_STOP_WAIT_MS     50
#define IR_FILE_MAX_SIZE       4096
#define SUB_PATH_MAX_LEN       512
#define FILE_PATH_MAX_LEN      600
#define IR_FILE_EXT            ".ir"
#define IR_FILE_EXT_LEN        3

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_count_label = NULL;
static spinner_ui_t s_spinner;

static bool s_btn_back_last = false;
static TaskHandle_t s_burst_task = NULL;
static volatile bool s_is_done = false;
static volatile bool s_stop_requested = false;
static volatile int s_sent_count = 0;
static volatile int s_total_count = 0;

static void send_one_file(const char *path);
static void burst_task(void *pvParameters);
static void nav_timer_cb(lv_timer_t *timer);

void ui_ir_burst_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_is_done = false;
  s_stop_requested = false;
  s_sent_count = 0;
  s_total_count = 0;
  s_burst_task = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, OUTER_BORDER, 0);
  lv_obj_set_style_border_color(s_screen, current_theme.border_interface, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *top_area = lv_obj_create(s_screen);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, TOP_AREA_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_BAR_W, TITLE_BAR_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, TITLE_BAR_RADIUS, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, TITLE_BAR_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, "IR BURST");
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, "Burst running...");
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, STATUS_LABEL_OFFSET_Y);

  s_count_label = lv_label_create(s_screen);
  lv_label_set_text(s_count_label, "Scanning files...");
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_count_label, LV_ALIGN_CENTER, 0, COUNT_LABEL_OFFSET_Y);

  s_spinner = spinner_ui_create(s_screen, SPINNER_SIZE);
  lv_obj_align(s_spinner.obj, LV_ALIGN_BOTTOM_MID, 0, SPINNER_OFFSET_Y);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  xTaskCreate(
      burst_task, "ir_burst", BURST_TASK_STACK_SIZE, NULL, BURST_TASK_PRIORITY, &s_burst_task);

  lv_screen_load(s_screen);
}

static void send_one_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (f == NULL)
    return;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (sz <= 0 || sz > IR_FILE_MAX_SIZE) {
    fclose(f);
    return;
  }

  char *buf = malloc(sz + 1);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buffer for %s", path);
    fclose(f);
    return;
  }

  fread(buf, 1, sz, f);
  buf[sz] = '\0';
  fclose(f);

  ir_file_t ir_file;
  ir_file_init(&ir_file);

  if (ir_file_parse(buf, &ir_file)) {
    for (size_t i = 0; i < ir_file.count && !s_stop_requested; i++) {
      ir_file_send(&ir_file.signals[i]);
      vTaskDelay(pdMS_TO_TICKS(BURST_DELAY_MS));
    }
    s_sent_count++;
  }

  ir_file_free(&ir_file);
  free(buf);
}

static void burst_task(void *pvParameters) {
  (void)pvParameters;

  ir_tx_init();

  DIR *root = opendir(TOS_PATH_IR);
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to open IR path: %s", TOS_PATH_IR);
    s_is_done = true;
    s_burst_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  int total = 0;
  struct dirent *proto_ent;

  while ((proto_ent = readdir(root)) != NULL) {
    if (proto_ent->d_name[0] == '.' || proto_ent->d_type != DT_DIR)
      continue;

    char sub_path[SUB_PATH_MAX_LEN];
    snprintf(sub_path, sizeof(sub_path), TOS_PATH_IR "/%.64s", proto_ent->d_name);

    DIR *sub = opendir(sub_path);
    if (sub == NULL)
      continue;

    struct dirent *fe;
    while ((fe = readdir(sub)) != NULL) {
      size_t len = strlen(fe->d_name);
      if (len >= IR_FILE_EXT_LEN + 1 &&
          strcmp(fe->d_name + len - IR_FILE_EXT_LEN, IR_FILE_EXT) == 0)
        total++;
    }
    closedir(sub);
  }

  s_total_count = total;
  rewinddir(root);

  while ((proto_ent = readdir(root)) != NULL && !s_stop_requested) {
    if (proto_ent->d_name[0] == '.' || proto_ent->d_type != DT_DIR)
      continue;

    char sub_path[SUB_PATH_MAX_LEN];
    snprintf(sub_path, sizeof(sub_path), TOS_PATH_IR "/%.64s", proto_ent->d_name);

    DIR *sub = opendir(sub_path);
    if (sub == NULL)
      continue;

    struct dirent *fe;
    while ((fe = readdir(sub)) != NULL && !s_stop_requested) {
      size_t len = strlen(fe->d_name);
      if (len < IR_FILE_EXT_LEN + 1 || strcmp(fe->d_name + len - IR_FILE_EXT_LEN, IR_FILE_EXT) != 0)
        continue;

      char file_path[FILE_PATH_MAX_LEN];
      snprintf(file_path, sizeof(file_path), "%.299s/%.255s", sub_path, fe->d_name);
      send_one_file(file_path);
    }
    closedir(sub);
  }

  closedir(root);
  s_is_done = true;
  s_burst_task = NULL;
  vTaskDelete(NULL);
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }

  if (ui_input_is_locked())
    return;

  if (msgbox_is_open())
    return;

  if (!s_is_done && s_total_count > 0)
    lv_label_set_text_fmt(s_count_label, "%d / %d files", s_sent_count, s_total_count);

  if (s_is_done) {
    s_is_done = false;
    spinner_ui_hide(&s_spinner);
    lv_label_set_text(s_status_label, "Burst complete!");
    lv_label_set_text_fmt(s_count_label, "%d signals sent", s_sent_count);
  }

  bool is_back = back_button_is_down();
  if (is_back && !s_btn_back_last) {
    s_stop_requested = true;
    if (s_burst_task != NULL) {
      vTaskDelay(pdMS_TO_TICKS(BURST_STOP_WAIT_MS));
      if (s_burst_task != NULL) {
        vTaskDelete(s_burst_task);
        s_burst_task = NULL;
      }
    }
    ui_switch_screen(SCREEN_IR_MENU);
  }

  s_btn_back_last = is_back;
}
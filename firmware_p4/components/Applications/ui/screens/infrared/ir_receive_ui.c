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

#include "ir_receive_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ir.h"
#include "ir_file.h"
#include "ir_protocol.h"
#include "keyboard_ui.h"
#include "msgbox_ui.h"
#include "spinner_ui.h"
#include "storage_mkdir.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_RX_UI";

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2
#define STATUS_LABEL_OFFSET_Y  12
#define DETAIL_LABEL_OFFSET_Y  35
#define DETAIL_LABEL_MARGIN    20
#define SPINNER_SIZE           30
#define SPINNER_OFFSET_Y       (-20)
#define NAV_TIMER_INTERVAL_MS  50
#define KB_OPEN_DELAY_MS       300
#define RX_TASK_STACK_SIZE     4096
#define RX_TASK_PRIORITY       5
#define RX_TIMEOUT_MS          15000
#define IR_DIR_MAX_LEN         300
#define IR_PATH_MAX_LEN        300
#define IR_BUF_MAX_LEN         512
#define IR_DETAIL_BUF_LEN      128

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_detail_label = NULL;
static spinner_ui_t s_spinner;

static bool s_btn_back_last = false;
static bool s_btn_ok_last = false;
static TaskHandle_t s_rx_task_handle = NULL;
static volatile bool s_is_rx_done = false;
static volatile bool s_is_rx_success = false;
static ir_data_t s_rx_result;

static void rx_task(void *pvParameters);
static void on_save_result(bool is_confirm);
static void on_name_entered(const char *text, void *user_data);
static void deferred_kb_open(lv_timer_t *timer);
static void on_ask_save(bool is_confirm);
static void show_waiting(void);
static void show_result(void);
static void nav_timer_cb(lv_timer_t *timer);

void ui_ir_receive_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_is_rx_done = false;
  s_is_rx_success = false;
  s_rx_task_handle = NULL;

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
  lv_label_set_text(title_lbl, "IR LEARN");
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, "Press OK to start");
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + STATUS_LABEL_OFFSET_Y);

  s_detail_label = lv_label_create(s_screen);
  lv_label_set_text(s_detail_label, "");
  lv_obj_set_style_text_color(s_detail_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s_detail_label, LCD_H_RES - OUTER_BORDER * 2 - DETAIL_LABEL_MARGIN);
  lv_obj_align(s_detail_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + DETAIL_LABEL_OFFSET_Y);

  s_spinner = spinner_ui_create(s_screen, SPINNER_SIZE);
  lv_obj_align(s_spinner.obj, LV_ALIGN_BOTTOM_MID, 0, SPINNER_OFFSET_Y);
  spinner_ui_hide(&s_spinner);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  lv_screen_load(s_screen);
}

static void rx_task(void *pvParameters) {
  (void)pvParameters;
  ir_rx_init();
  s_is_rx_success = ir_receive(&s_rx_result, RX_TIMEOUT_MS);
  s_is_rx_done = true;
  s_rx_task_handle = NULL;
  vTaskDelete(NULL);
}

static void on_save_result(bool is_confirm) {
  if (is_confirm) {
    if (s_status_label != NULL)
      lv_label_set_text(s_status_label, "Press OK to start");
    if (s_detail_label != NULL)
      lv_label_set_text(s_detail_label, "");
  } else {
    ui_switch_screen(SCREEN_IR_MENU);
  }
}

static void on_name_entered(const char *text, void *user_data) {
  (void)user_data;

  if (text == NULL || strlen(text) == 0)
    return;

  ir_file_t file;
  ir_file_init(&file);
  ir_file_add_parsed(&file, text, &s_rx_result);

  char buf[IR_BUF_MAX_LEN];
  size_t len = ir_file_to_string(&file, buf, sizeof(buf));
  bool is_saved = false;

  if (len > 0) {
    const char *proto = ir_protocol_name(s_rx_result.protocol);

    char dir[IR_DIR_MAX_LEN];
    snprintf(dir, sizeof(dir), TOS_PATH_IR "/%.64s", proto);
    storage_mkdir_recursive(dir);

    char path[IR_PATH_MAX_LEN];
    snprintf(path, sizeof(path), TOS_PATH_IR "/%.64s/%.64s.ir", proto, text);

    FILE *f = fopen(path, "w");
    if (f != NULL) {
      fwrite(buf, 1, len, f);
      fclose(f);
      is_saved = true;
      ESP_LOGI(TAG, "Saved: %s", path);
    }
  }

  ir_file_free(&file);

  if (is_saved)
    msgbox_open(LV_SYMBOL_OK, "Signal saved!", "Continue", "Exit", on_save_result);
  else
    msgbox_open(LV_SYMBOL_WARNING, "Failed to save!", "Continue", "Exit", on_save_result);
}

static void deferred_kb_open(lv_timer_t *timer) {
  (void)timer;
  keyboard_open(NULL, on_name_entered, NULL);
}

static void on_ask_save(bool is_confirm) {
  if (is_confirm) {
    lv_timer_t *kb_timer = lv_timer_create(deferred_kb_open, KB_OPEN_DELAY_MS, NULL);
    lv_timer_set_repeat_count(kb_timer, 1);
  } else {
    if (s_status_label != NULL)
      lv_label_set_text(s_status_label, "Press OK to start");
    if (s_detail_label != NULL)
      lv_label_set_text(s_detail_label, "");
  }
}

static void show_waiting(void) {
  if (s_status_label != NULL)
    lv_label_set_text(s_status_label, "Waiting for signal...");
  if (s_detail_label != NULL)
    lv_label_set_text(s_detail_label, "Point remote at device");
  spinner_ui_show(&s_spinner);
}

static void show_result(void) {
  spinner_ui_hide(&s_spinner);

  if (s_is_rx_success) {
    lv_label_set_text(s_status_label, "Signal captured!");

    char buf[IR_DETAIL_BUF_LEN];
    snprintf(buf,
             sizeof(buf),
             "Protocol: %s\nAddress: 0x%04X\nCommand: 0x%04X",
             ir_protocol_name(s_rx_result.protocol),
             s_rx_result.address,
             s_rx_result.command);
    lv_label_set_text(s_detail_label, buf);

    msgbox_open(LV_SYMBOL_OK, "Save signal?", "Yes", "No", on_ask_save);
  } else {
    lv_label_set_text(s_status_label, "No signal detected");
    lv_label_set_text(s_detail_label, "Press OK to try again");
  }
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

  if (s_is_rx_done) {
    s_is_rx_done = false;
    show_result();
  }

  bool is_back = back_button_is_down();
  bool is_ok = ok_button_is_down();

  if (is_back && !s_btn_back_last) {
    if (s_rx_task_handle != NULL) {
      vTaskDelete(s_rx_task_handle);
      s_rx_task_handle = NULL;
    }
    ui_switch_screen(SCREEN_IR_MENU);
  }

  if (is_ok && !s_btn_ok_last && s_rx_task_handle == NULL) {
    s_is_rx_done = false;
    s_is_rx_success = false;
    show_waiting();
    xTaskCreate(rx_task, "ir_rx", RX_TASK_STACK_SIZE, NULL, RX_TASK_PRIORITY, &s_rx_task_handle);
  }

  s_btn_back_last = is_back;
  s_btn_ok_last = is_ok;
}
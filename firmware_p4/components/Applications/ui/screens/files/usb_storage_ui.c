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

#include "usb_storage_ui.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "sys_prio.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "usb_msc.h"

static const char *TAG = "USB_STORAGE_UI";

#define VIEW_W LCD_PANEL_W
#define VIEW_H LCD_PANEL_H

#define COL_READY     lv_color_hex(0x00BCD4)
#define COL_CONNECTED lv_color_hex(0x00E676)
#define COL_ERROR     lv_color_hex(0xFF5252)

#define USB_MSC_TASK_STACK 8192
#define REFRESH_PERIOD_MS  200

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_hint = NULL;
static lv_timer_t *s_timer = NULL;
static volatile bool s_exit_requested = false;

static void enter_task(void *a) {
  (void)a;
  usb_msc_enter();
  vTaskDelete(NULL);
}
static void exit_task(void *a) {
  (void)a;
  usb_msc_exit();
  vTaskDelete(NULL);
}

static void set_texts(const char *status, lv_color_t col, const char *hint) {
  if (s_status) {
    lv_label_set_text(s_status, status);
    lv_obj_set_style_text_color(s_status, col, 0);
  }
  if (s_hint)
    lv_label_set_text(s_hint, hint);
}

static void refresh_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  switch (usb_msc_get_state()) {
    case USB_MSC_ACTIVE:
      if (usb_msc_host_connected())
        set_texts("Connected", COL_CONNECTED, "Copy your files, then press BACK to eject.");
      else
        set_texts("Ready", COL_READY, "Open the drive on your PC.\nBACK ejects and returns.");
      break;
    case USB_MSC_ERROR:
      set_texts("Couldn't start USB storage", COL_ERROR, "Press BACK to return.");
      break;
    case USB_MSC_EXITING:
      set_texts("Ejecting...", current_theme.text_secondary, "Restoring the card.");
      break;
    case USB_MSC_IDLE:
      if (s_exit_requested)
        ui_switch_screen(SCREEN_FILES);
      else
        set_texts("Starting...", current_theme.text_secondary, "Preparing the USB drive.");
      break;
    case USB_MSC_ENTERING:
    default:
      set_texts("Starting...", current_theme.text_secondary, "Preparing the USB drive.");
      break;
  }
}

static void usb_storage_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS || ev->button != INPUT_BTN_BACK)
    return;
  switch (usb_msc_get_state()) {
    case USB_MSC_ACTIVE:
      s_exit_requested = true;
      set_texts("Ejecting...", current_theme.text_secondary, "Restoring the card.");
      ui_feedback(UI_FB_SELECT);
      xTaskCreatePinnedToCore(exit_task, "usb_msc_x", USB_MSC_TASK_STACK, NULL, SYS_PRIO_SERVICE_HI, NULL,
                              SYS_CORE_RADIO);
      break;
    case USB_MSC_ERROR:
    case USB_MSC_IDLE:
      ui_switch_screen(SCREEN_FILES);
      break;
    default:
      break;
  }
}

void ui_usb_storage_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_exit_requested = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_screen, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, VIEW_W - 36, 236);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 24, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
  lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 10, 0);

  lv_image_dsc_t *ic = assets_get("/assets/icons/usb.bin");
  if (ic != NULL) {
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, ic);
    lv_image_set_scale(img, 384);
  }

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "USB Storage");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);

  s_status = lv_label_create(card);
  lv_obj_set_width(s_status, VIEW_W - 80);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_label_set_text(s_status, "Starting...");
  lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);

  s_hint = lv_label_create(card);
  lv_obj_set_width(s_hint, VIEW_W - 72);
  lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
  lv_label_set_text(s_hint, "Preparing the USB drive.");
  lv_obj_set_style_text_color(s_hint, current_theme.text_secondary, 0);

  ui_input_set_screen_handler(usb_storage_input, NULL);
  s_timer = lv_timer_create(refresh_cb, REFRESH_PERIOD_MS, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  xTaskCreatePinnedToCore(enter_task, "usb_msc_e", USB_MSC_TASK_STACK, NULL, SYS_PRIO_SERVICE_HI, NULL,
                          SYS_CORE_RADIO);
}

void ui_usb_storage_stop(void) {
  s_status = NULL;
  s_hint = NULL;
  if (usb_msc_get_state() == USB_MSC_ACTIVE) {
    ESP_LOGW(TAG, "closed while active - rebooting to restore /sdcard");
    esp_restart();
  }
}

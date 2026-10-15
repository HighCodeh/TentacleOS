// Copyright (c) 2025 HIGH CODE LLC

#include "ble_beacon_ui.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "bluetooth_service.h"
#include "notify_ui.h"
#include "sys_prio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

#define ADV_TASK_STACK 4096

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_details;
static volatile bool s_adv_active;
static volatile esp_err_t s_adv_result;
static char s_mac[18];

static void advertiser_input(const input_event_t *ev, void *ctx);

static void advertiser_started_cb(void *arg) {
  (void)arg;
  if (ui_current_screen() != SCREEN_BLE_BEACON_SPAM || s_status == NULL)
    return;

  if (s_adv_result == ESP_OK) {
    lv_label_set_text(s_status, LV_SYMBOL_BLUETOOTH "  ADVERTISING");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x00E676), 0);
    lv_label_set_text_fmt(
        s_details, "Connectable advertiser\n%s\nDevice Information service", s_mac);
    ui_feedback(UI_FB_EMULATE);
    notify(NOTIFY_INFO, "BLE advertiser started");
  } else {
    lv_label_set_text(s_status, LV_SYMBOL_CLOSE "  RADIO ERROR");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFF6B6B), 0);
    lv_label_set_text_fmt(s_details, "C5 returned\n%s", esp_err_to_name(s_adv_result));
    notify(NOTIFY_WARNING, "BLE advertising failed");
  }
}

static void advertiser_start_task(void *arg) {
  (void)arg;
  esp_err_t err = bluetooth_service_init();
  if (err == ESP_OK)
    err = bluetooth_service_start();
  if (err == ESP_OK)
    err = bluetooth_service_start_advertising();

  if (err == ESP_OK) {
    uint8_t mac[6] = {0};
    bluetooth_service_get_mac(mac);
    snprintf(s_mac,
             sizeof(s_mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[5],
             mac[4],
             mac[3],
             mac[2],
             mac[1],
             mac[0]);
    s_adv_active = true;
  }
  s_adv_result = err;
  ui_async_call(advertiser_started_cb, NULL);
  vTaskDelete(NULL);
}

static void advertiser_stop_task(void *arg) {
  (void)arg;
  bluetooth_service_stop_advertising();
  s_adv_active = false;
  vTaskDelete(NULL);
}

void ui_beacon_spam_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_adv_active = false;
  s_adv_result = ESP_ERR_INVALID_STATE;
  strcpy(s_mac, "--:--:--:--:--:--");

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "BLE ADVERTISER", "/assets/icons/sensors.bin");
  ui_chrome_footer(s_screen, "BACK Stop advertising");
  waves_create(s_screen, LV_ALIGN_CENTER, 0, -46, LV_SYMBOL_BLUETOOTH, "/assets/icons/sensors.bin");

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, "Starting C5 radio...");
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 48);

  s_details = lv_label_create(s_screen);
  lv_label_set_text(s_details, "Waiting for hardware response");
  lv_obj_set_style_text_color(s_details, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(s_details, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_details, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_details, LV_ALIGN_CENTER, 0, 91);

  ui_input_set_screen_handler(advertiser_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  if (xTaskCreatePinnedToCore(advertiser_start_task,
                              "ble_adv",
                              ADV_TASK_STACK,
                              NULL,
                              SYS_PRIO_SERVICE_LO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    s_adv_result = ESP_ERR_NO_MEM;
    advertiser_started_cb(NULL);
  }
}

static void advertiser_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->button != INPUT_BTN_BACK || ev->action != INPUT_ACTION_PRESS)
    return;

  if (s_adv_active) {
    xTaskCreatePinnedToCore(advertiser_stop_task,
                            "ble_adv_stop",
                            ADV_TASK_STACK,
                            NULL,
                            SYS_PRIO_SERVICE_LO,
                            NULL,
                            SYS_CORE_RADIO);
  }
  ui_switch_screen(SCREEN_BLE_MENU);
}

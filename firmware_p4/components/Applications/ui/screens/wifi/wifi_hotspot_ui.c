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

#include "wifi_hotspot_ui.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "led_control.h"
#include "menu_component_ui.h"
#include "sys_prio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "wifi_service.h"

#define AP_NAME            "HighBoy_AP"
#define AP_IP              "192.168.4.1"
#define AP_MAX_CONNECTIONS 8
#define TASK_STACK_SIZE    4096
#define REFRESH_MS         250

static const char *TAG = "HOTSPOT_UI";
static lv_obj_t *s_screen;
static lv_timer_t *s_timer;
static EXT_RAM_BSS_ATTR menu_component_t s_menu;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_requested, s_busy, s_online;
static esp_err_t s_error;

static void apply_task(void *unused) {
  (void)unused;
  for (;;) {
    portENTER_CRITICAL(&s_lock);
    bool requested = s_requested;
    portEXIT_CRITICAL(&s_lock);
    esp_err_t error =
        requested ? wifi_service_save_ap_config(AP_NAME, NULL, AP_MAX_CONNECTIONS, AP_IP, true)
                  : wifi_service_set_enabled(false);
    portENTER_CRITICAL(&s_lock);
    s_error = error;
    if (error == ESP_OK)
      s_online = requested;
    bool again = s_requested != requested;
    if (!again)
      s_busy = false;
    portEXIT_CRITICAL(&s_lock);
    if (error != ESP_OK) {
      ESP_LOGE(TAG, "Hotspot change failed: %s", esp_err_to_name(error));
      led_signal_error();
    } else {
      ESP_LOGI(TAG, "Hotspot %s", requested ? "enabled" : "disabled");
      led_signal_info();
    }
    if (!again)
      break;
  }
  vTaskDeleteWithCaps(NULL);
}

static void request(bool enabled) {
  portENTER_CRITICAL(&s_lock);
  s_requested = enabled;
  bool start = !s_busy;
  s_busy = true;
  portEXIT_CRITICAL(&s_lock);
  if (start && xTaskCreatePinnedToCoreWithCaps(apply_task,
                                               "ap_ctl",
                                               TASK_STACK_SIZE,
                                               NULL,
                                               SYS_PRIO_SERVICE_LO,
                                               NULL,
                                               SYS_CORE_RADIO,
                                               MALLOC_CAP_SPIRAM) != pdPASS) {
    portENTER_CRITICAL(&s_lock);
    s_busy = false;
    s_error = ESP_ERR_NO_MEM;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGE(TAG, "Hotspot task allocation failed");
    led_signal_error();
  }
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  portENTER_CRITICAL(&s_lock);
  bool busy = s_busy, online = s_online;
  esp_err_t error = s_error;
  portEXIT_CRITICAL(&s_lock);
  menu_component_set_selector_value(&s_menu, 0, busy ? "Please wait" : online ? "Stop" : "Start");
  menu_component_set_selector_value(&s_menu,
                                    1,
                                    busy              ? "Applying..."
                                    : error != ESP_OK ? "Failed - retry"
                                    : online          ? "Online"
                                                      : "Offline");
  menu_component_set_hint(&s_menu,
                          menu_component_get_selected(&s_menu) == 0 ? "OK start/stop   BACK exit"
                                                                    : "UP/DOWN view   BACK exit");
}

void ui_wifi_hotspot_stop(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  portENTER_CRITICAL(&s_lock);
  bool active = s_requested || s_online;
  portEXIT_CRITICAL(&s_lock);
  if (active)
    request(false);
}

static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  ui_wifi_hotspot_stop();
  s_screen = NULL;
}

static void input(const input_event_t *event, void *context) {
  (void)context;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (event->button == INPUT_BTN_BACK && press) {
    ui_switch_screen(SCREEN_WIFI_MENU);
    return;
  }
  if (event->button == INPUT_BTN_UP && nav)
    menu_component_prev(&s_menu);
  if (event->button == INPUT_BTN_DOWN && nav)
    menu_component_next(&s_menu);
  if (event->button == INPUT_BTN_OK && press && menu_component_get_selected(&s_menu) == 0) {
    portENTER_CRITICAL(&s_lock);
    bool busy = s_busy, enabled = !s_online;
    portEXIT_CRITICAL(&s_lock);
    if (!busy && s_timer != NULL)
      request(enabled);
  }
  refresh(NULL);
}

void ui_wifi_hotspot_open(void) {
  ui_wifi_hotspot_stop();
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_screen = ui_chrome_screen_create();
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  s_menu = menu_component_create(s_screen, "Hotspot", "/assets/icons/wifi_tethering.bin");
  menu_component_add_selector(&s_menu, NULL, "Hotspot", "Start");
  menu_component_add_selector(&s_menu, NULL, "Status", "Offline");
  menu_component_add_selector(&s_menu, NULL, "Network", AP_NAME);
  menu_component_add_selector(&s_menu, NULL, "Security", "Open");
  menu_component_add_selector(&s_menu, NULL, "Gateway", AP_IP);
  menu_component_select(&s_menu, 0);
  s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
  if (s_timer == NULL) {
    ESP_LOGE(TAG, "Hotspot refresh allocation failed");
    led_signal_error();
    menu_component_set_hint(&s_menu, "Unavailable - BACK to retry");
  } else {
    refresh(NULL);
  }
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

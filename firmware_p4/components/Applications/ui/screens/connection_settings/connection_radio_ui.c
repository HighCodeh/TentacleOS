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

#include "connection_radio_ui.h"

#include <string.h>
#include <stdio.h>

#include "esp_attr.h"
#include "esp_log.h"

#include "connection_settings.h"
#include "keyboard_ui.h"
#include "led_control.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"

#define SETTINGS_REFRESH_MS    100
#define SETTINGS_NUMBER_LENGTH 4
#define SETTINGS_SSID_LENGTH   32

static const char *TAG = "CONNECTION_RADIO_UI";

typedef enum {
  CONNECTION_RADIO_POWER,
  CONNECTION_RADIO_NAME,
  CONNECTION_RADIO_LIMIT,
  CONNECTION_RADIO_OPTION,
  CONNECTION_RADIO_APPLY,
  CONNECTION_RADIO_STATUS,
  CONNECTION_RADIO_JOIN,
  CONNECTION_RADIO_COUNT
} connection_radio_row_t;

static lv_obj_t *s_screen;
static lv_timer_t *s_timer;
static lv_obj_t *s_status_label;
static EXT_RAM_BSS_ATTR menu_component_t s_menu;
static tos_config_wifi_t s_wifi;
static tos_config_ble_t s_ble;
static connection_settings_kind_t s_kind;
static bool s_is_open;
static bool s_is_dirty;
static bool s_has_result;
static uint32_t s_revision;
static const char *s_result;

static void open(connection_settings_kind_t kind);
static void refresh(lv_timer_t *timer);
static void input(const input_event_t *event, void *context);
static void deleted(lv_event_t *event);
static void edit_text(connection_radio_row_t row);
static void submitted(const char *text, void *context);
static void changed(void);

void ui_connection_radio_wifi_open(void) {
  open(CONNECTION_SETTINGS_WIFI);
}

void ui_connection_radio_ble_open(void) {
  open(CONNECTION_SETTINGS_BLE);
}

void ui_connection_radio_close(void) {
  s_is_open = false;
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (keyboard_is_open())
    keyboard_close();
  memset(s_wifi.ap.password, 0, sizeof(s_wifi.ap.password));
}

static void open(connection_settings_kind_t kind) {
  ui_connection_radio_close();
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_kind = kind;
  s_is_dirty = false;
  s_has_result = false;
  connection_settings_get_config(&s_wifi, &s_ble);
  connection_settings_status_t status;
  connection_settings_get_status(&status);
  s_revision = status.has_result && status.kind == kind ? status.revision - 1 : status.revision;
  s_screen = ui_chrome_screen_create();
  if (s_screen == NULL) {
    ESP_LOGE(TAG, "Cannot allocate radio settings screen");
    led_signal_error();
    ui_input_set_screen_handler(input, NULL);
    return;
  }
  s_is_open = true;
  bool is_wifi = kind == CONNECTION_SETTINGS_WIFI;
  s_menu =
      menu_component_create(s_screen,
                            is_wifi ? "WI-FI SETTINGS" : "BLUETOOTH SETTINGS",
                            is_wifi ? "/assets/icons/wifi.bin" : "/assets/icons/bluetooth.bin");
  menu_component_add_toggle(&s_menu, NULL, "Radio", is_wifi ? s_wifi.enabled : s_ble.enabled);
  menu_component_add_selector(&s_menu, NULL, is_wifi ? "Network name" : "Device name", "");
  menu_component_add_selector(&s_menu, NULL, "Max clients", "");
  if (is_wifi)
    menu_component_add_selector(&s_menu, NULL, "Password", "");
  else
    menu_component_add_toggle(&s_menu, NULL, "Visible", s_ble.advertising);
  menu_component_add_item(&s_menu, NULL, "Save & apply");
  lv_obj_t *status_row = menu_component_add_item(&s_menu, NULL, "Saved settings");
  s_status_label = lv_obj_get_child(status_row, 0);
  if (is_wifi)
    menu_component_add_item(&s_menu, "/assets/icons/wifi.bin", "Connect to network");
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  s_timer = lv_timer_create(refresh, SETTINGS_REFRESH_MS, NULL);
  if (s_timer == NULL) {
    ESP_LOGE(TAG, "Cannot allocate settings refresh timer");
    led_signal_error();
    s_has_result = true;
    s_result = "Unavailable; BACK to retry";
  }
  refresh(NULL);
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  if (!s_is_open)
    return;
  connection_settings_status_t status;
  connection_settings_get_status(&status);
  if (status.revision != s_revision) {
    s_revision = status.revision;
    if (status.kind == s_kind && status.has_result) {
      s_has_result = true;
      s_result = status.error == ESP_OK ? "Applied and saved"
                 : status.is_applied    ? "Applied; save failed"
                                        : "Failed; retry Apply";
      if (status.is_applied) {
        connection_settings_get_config(&s_wifi, &s_ble);
        s_is_dirty = status.error != ESP_OK;
      }
    }
  }
  bool is_wifi = s_kind == CONNECTION_SETTINGS_WIFI;
  char clients[SETTINGS_NUMBER_LENGTH];
  snprintf(
      clients, sizeof(clients), "%u", (unsigned)(is_wifi ? s_wifi.ap.max_conn : s_ble.max_conn));
  menu_component_set_toggle(
      &s_menu, CONNECTION_RADIO_POWER, is_wifi ? s_wifi.enabled : s_ble.enabled);
  menu_component_set_selector_value(
      &s_menu, CONNECTION_RADIO_NAME, is_wifi ? s_wifi.ap.ssid : s_ble.name);
  menu_component_set_selector_value(&s_menu, CONNECTION_RADIO_LIMIT, clients);
  if (is_wifi)
    menu_component_set_selector_value(
        &s_menu, CONNECTION_RADIO_OPTION, s_wifi.ap.password[0] ? "********" : "Open network");
  else
    menu_component_set_toggle(&s_menu, CONNECTION_RADIO_OPTION, s_ble.advertising);
  const char *text = status.is_busy ? "Applying..."
                     : s_has_result ? s_result
                     : s_is_dirty   ? "Unsaved changes"
                                    : "Saved settings";
  lv_label_set_text(s_status_label, text);
  menu_component_set_hint(&s_menu,
                          status.is_busy ? "Applying...   BACK Exit"
                          : s_is_dirty   ? "Save & apply   BACK Cancel"
                                         : "OK Edit   L/R Change   BACK");
}

static void changed(void) {
  s_is_dirty = true;
  s_has_result = false;
  refresh(NULL);
}

static void submitted(const char *text, void *context) {
  if (!s_is_open || text == NULL)
    return;
  connection_radio_row_t row = (connection_radio_row_t)(intptr_t)context;
  size_t length = strlen(text);
  bool is_password = row == CONNECTION_RADIO_OPTION;
  size_t maximum = is_password                          ? CONNECTION_SETTINGS_PASSWORD_MAX
                   : s_kind == CONNECTION_SETTINGS_WIFI ? SETTINGS_SSID_LENGTH
                                                        : CONNECTION_SETTINGS_NAME_MAX;
  if (length > maximum || (!is_password && length == 0) ||
      (is_password && length != 0 && length < CONNECTION_SETTINGS_PASSWORD_MIN)) {
    notify(NOTIFY_WARNING,
           is_password ? "Password: empty or 8-63 bytes" : "Name is too long or empty");
    return;
  }
  char *destination = is_password                          ? s_wifi.ap.password
                      : s_kind == CONNECTION_SETTINGS_WIFI ? s_wifi.ap.ssid
                                                           : s_ble.name;
  memcpy(destination, text, length + 1);
  changed();
}

static void edit_text(connection_radio_row_t row) {
  bool is_password = row == CONNECTION_RADIO_OPTION;
  bool is_wifi = s_kind == CONNECTION_SETTINGS_WIFI;
  keyboard_config_t editor = {
      .title = is_password ? "Hotspot password (empty = open)"
               : is_wifi   ? "Wi-Fi network name"
                           : "Bluetooth name",
      .initial = is_password ? s_wifi.ap.password
                 : is_wifi   ? s_wifi.ap.ssid
                             : s_ble.name,
      .max_chars = is_password ? CONNECTION_SETTINGS_PASSWORD_MAX
                   : is_wifi   ? SETTINGS_SSID_LENGTH
                               : CONNECTION_SETTINGS_NAME_MAX,
      .min_chars = is_password ? 0 : 1,
      .mode = is_password ? KEYBOARD_PASSWORD : KEYBOARD_TEXT,
  };
  keyboard_open_config(&editor, submitted, (void *)(intptr_t)row);
}

static void input(const input_event_t *event, void *context) {
  (void)context;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool is_navigation = is_press || event->action == INPUT_ACTION_REPEAT;
  if (event->button == INPUT_BTN_BACK && is_press) {
    ui_switch_screen(SCREEN_CONNECTION_SETTINGS);
    return;
  }
  if (!s_is_open)
    return;
  if (event->button == INPUT_BTN_UP && is_navigation)
    menu_component_prev(&s_menu);
  else if (event->button == INPUT_BTN_DOWN && is_navigation)
    menu_component_next(&s_menu);
  else if (is_press) {
    connection_settings_status_t status;
    connection_settings_get_status(&status);
    if (status.is_busy || s_timer == NULL)
      return;
    int selected = menu_component_get_selected(&s_menu);
    bool is_ok = event->button == INPUT_BTN_OK;
    bool is_change = is_ok || event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT;
    bool is_wifi = s_kind == CONNECTION_SETTINGS_WIFI;
    if (selected == CONNECTION_RADIO_POWER && is_change) {
      if (is_wifi)
        s_wifi.enabled = !s_wifi.enabled;
      else
        s_ble.enabled = !s_ble.enabled;
      changed();
    } else if (selected == CONNECTION_RADIO_LIMIT && is_change) {
      int value = is_wifi ? s_wifi.ap.max_conn : s_ble.max_conn;
      int maximum =
          is_wifi ? CONNECTION_SETTINGS_WIFI_MAX_CLIENTS : CONNECTION_SETTINGS_BLE_MAX_CLIENTS;
      value += event->button == INPUT_BTN_LEFT ? -1 : 1;
      if (value < 1)
        value = maximum;
      if (value > maximum)
        value = 1;
      if (is_wifi)
        s_wifi.ap.max_conn = value;
      else
        s_ble.max_conn = (uint8_t)value;
      changed();
    } else if (selected == CONNECTION_RADIO_OPTION && !is_wifi && is_change) {
      s_ble.advertising = !s_ble.advertising;
      changed();
    } else if ((selected == CONNECTION_RADIO_NAME ||
                (selected == CONNECTION_RADIO_OPTION && is_wifi)) &&
               is_ok) {
      edit_text((connection_radio_row_t)selected);
    } else if (selected == CONNECTION_RADIO_APPLY && is_ok) {
      esp_err_t error = is_wifi ? connection_settings_request_wifi(&s_wifi)
                                : connection_settings_request_ble(&s_ble);
      if (error == ESP_OK) {
        s_has_result = false;
      } else {
        s_has_result = true;
        s_result = "Failed; check values / retry";
        ESP_LOGE(TAG, "Settings request failed: %s", esp_err_to_name(error));
        led_signal_error();
      }
    } else if (selected == CONNECTION_RADIO_JOIN && is_wifi && is_ok) {
      if (s_is_dirty)
        notify(NOTIFY_WARNING, "Apply or discard edits before connecting");
      else
        ui_switch_screen(SCREEN_CONNECT_WIFI);
      return;
    }
  }
  refresh(NULL);
}

static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen) {
    ui_connection_radio_close();
    s_screen = NULL;
  }
}

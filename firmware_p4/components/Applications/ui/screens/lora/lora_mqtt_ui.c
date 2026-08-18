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

#include "lora_mqtt_ui.h"

#include <string.h>

#include "esp_log.h"

#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"

static const char *TAG = "LORA_MQTT";

#define FIELD_MAX    40
#define PASS_MASK    "******"
#define PASS_UNSET   "(unset)"
#define HOST_DEFAULT "mqtt.host"
#define USER_DEFAULT "highboy"

#define ICON_BROKER "/assets/icons/public.bin"
#define ICON_USER   "/assets/icons/hub.bin"
#define ICON_PASS   "/assets/icons/key.bin"
#define ICON_STATUS "/assets/icons/wifi_tethering.bin"
#define ICON_ACTION "/assets/icons/router.bin"
#define ICON_TITLE  "/assets/icons/lan.bin"

enum { ROW_BROKER = 0, ROW_USER, ROW_PASS, ROW_STATUS, ROW_ACTION, ROW_COUNT };

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static char s_broker[FIELD_MAX];
static char s_user[FIELD_MAX];
static char s_pass[FIELD_MAX];
static bool s_connected = false;
static int s_edit_field = ROW_BROKER;

static void lora_mqtt_input(const input_event_t *ev, void *ctx);

static const char *pass_display(void) {
  return (s_pass[0] == '\0') ? PASS_UNSET : PASS_MASK;
}

static void store_field(char *dst, const char *text) {
  strncpy(dst, text, FIELD_MAX - 1);
  dst[FIELD_MAX - 1] = '\0';
}

static void on_kb_submit(const char *text, void *user_data) {
  (void)user_data;
  if (text == NULL || text[0] == '\0')
    return;

  if (s_edit_field == ROW_BROKER) {
    store_field(s_broker, text);
    menu_component_set_selector_value(&s_menu, ROW_BROKER, s_broker);
  } else if (s_edit_field == ROW_USER) {
    store_field(s_user, text);
    menu_component_set_selector_value(&s_menu, ROW_USER, s_user);
  } else if (s_edit_field == ROW_PASS) {
    store_field(s_pass, text);
    menu_component_set_selector_value(&s_menu, ROW_PASS, pass_display());
  }
  ui_feedback(UI_FB_WRITE);
}

static void apply_status(void) {
  menu_component_set_selector_value(
      &s_menu, ROW_STATUS, s_connected ? "Connected" : "Disconnected");
  menu_component_set_selector_value(&s_menu, ROW_ACTION, s_connected ? "Disconnect" : "Connect");
  menu_component_set_item_label_color(
      &s_menu, ROW_STATUS, s_connected ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_secondary);
  menu_component_set_item_label_color(
      &s_menu, ROW_ACTION, s_connected ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_main);
}

static void toggle_connect(void) {
  s_connected = !s_connected;
  apply_status();
  if (s_connected) {
    ui_feedback(UI_FB_EMULATE);
    notify(NOTIFY_INFO, "MQTT bridge connected");
  } else {
    ui_feedback(UI_FB_SELECT);
    notify(NOTIFY_WARNING, "MQTT bridge stopped");
  }
}

static void build_screen(void) {
  s_menu = menu_component_create(s_screen, "MQTT BRIDGE", ICON_TITLE);
  menu_component_add_section(&s_menu, "BROKER");
  menu_component_add_selector(&s_menu, ICON_BROKER, "Broker", s_broker);
  menu_component_add_selector(&s_menu, ICON_USER, "User", s_user);
  menu_component_add_selector(&s_menu, ICON_PASS, "Pass", pass_display());
  menu_component_add_section(&s_menu, "LINK");
  menu_component_add_selector(&s_menu, ICON_STATUS, "Status", "Disconnected");
  menu_component_add_selector(&s_menu, ICON_ACTION, "Action", "Connect");
  menu_component_select(&s_menu, ROW_BROKER);
  menu_component_set_hint(&s_menu, LV_SYMBOL_UP LV_SYMBOL_DOWN "  move   OK edit/run   BACK exit");
  apply_status();
}

static void lora_mqtt_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_OK:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel == ROW_BROKER || sel == ROW_USER || sel == ROW_PASS) {
          s_edit_field = sel;
          ui_feedback(UI_FB_SELECT);
          keyboard_open(NULL, on_kb_submit, NULL);
        } else if (sel == ROW_ACTION) {
          toggle_connect();
        }
      }
      break;
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_LORA_CHAT);
      break;
    default:
      break;
  }
}

void ui_lora_mqtt_open(void) {
  ui_theme_set_protocol(PROTOCOL_LORA);

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  store_field(s_broker, HOST_DEFAULT);
  store_field(s_user, USER_DEFAULT);
  s_pass[0] = '\0';
  s_connected = false;
  s_edit_field = ROW_BROKER;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  build_screen();

  ui_input_set_screen_handler(lora_mqtt_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);

  ESP_LOGI(TAG, "LoRa MQTT bridge (mock) opened");
}

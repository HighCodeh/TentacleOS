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

#include "connection_settings_ui.h"
#include "esp_log.h"

#include "host_link.h"
#include "lv_port_indev.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "power_manager.h"
#include "tusb_desc.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define IDX_USB_NATIVE     0
#define USB_STATUS_POLL_MS 100
static const char *TAG = "CONNECTION_UI";
static lv_timer_t *s_usb_timer;
static bool s_usb_pending;
static bool s_usb_requested;

static lv_obj_t *s_screen_conn = NULL;
static menu_component_t s_menu;
static bool s_usb_no_sleep_held = false;

static void usb_status_cb(lv_timer_t *timer);

static void connection_settings_input(const input_event_t *ev, void *ctx);

void ui_connection_settings_open(void) {
  if (s_usb_timer == NULL)
    s_usb_timer = lv_timer_create(usb_status_cb, USB_STATUS_POLL_MS, NULL);
  if (s_screen_conn != NULL) {
    lv_obj_del(s_screen_conn);
    s_screen_conn = NULL;
  }

  s_screen_conn = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_conn, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen_conn, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen_conn, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen_conn, "CONNECTION", "/assets/icons/hub.bin");
  menu_component_add_toggle(&s_menu, "/assets/icons/usb.bin", "USB NATIVE", usb_mux_is_native());

  ui_input_set_screen_handler(connection_settings_input, NULL);

  ui_screen_load_owned(&s_screen_conn, s_screen_conn);
}

static void usb_status_cb(lv_timer_t *timer) {
  (void)timer;
  bool native = usb_mux_is_native();
  if (native && !s_usb_no_sleep_held) {
    power_manager_no_sleep_acquire();
    s_usb_no_sleep_held = true;
  } else if (!native && s_usb_no_sleep_held) {
    power_manager_no_sleep_release();
    s_usb_no_sleep_held = false;
  }
  if (s_usb_pending) {
    esp_err_t ret = usb_mux_result();
    if (ret == ESP_ERR_NOT_FINISHED)
      return;
    s_usb_pending = false;
    if (ret == ESP_OK && s_usb_requested)
      ret = host_link_cdc_init();
    if (ret != ESP_OK) {
      (void)usb_mux_request_native(false);
      ESP_LOGW(TAG, "USB switch: %s", esp_err_to_name(ret));
      notify(NOTIFY_WARNING, "USB indisponivel | tente novamente");
    } else {
      notify(NOTIFY_INFO, native ? "USB: native (serial off)" : "USB: UART bridge");
    }
  }
  if (s_screen_conn != NULL && lv_obj_is_valid(s_screen_conn) &&
      menu_component_get_toggle(&s_menu, IDX_USB_NATIVE) != native)
    menu_component_toggle_item(&s_menu, IDX_USB_NATIVE);
}

static void conn_toggle(int sel) {
  if (sel != IDX_USB_NATIVE || s_usb_pending)
    return;
  s_usb_requested = !usb_mux_is_native();
  esp_err_t ret = usb_mux_request_native(s_usb_requested);
  if (ret == ESP_OK) {
    s_usb_pending = true;
    notify(NOTIFY_INFO, "USB: alternando...");
  } else {
    notify(NOTIFY_WARNING, "USB ocupado ou indisponivel");
  }
}

static void connection_settings_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  int sel = menu_component_get_selected(&s_menu);

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS);
      break;
    case INPUT_BTN_LEFT:
    case INPUT_BTN_RIGHT:
    case INPUT_BTN_OK:
      if (press)
        conn_toggle(sel);
      break;
    default:
      break;
  }
}

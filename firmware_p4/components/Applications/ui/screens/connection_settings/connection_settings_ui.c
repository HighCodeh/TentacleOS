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
#include "esp_timer.h"

#include "buttons_gpio.h"
#include "lv_port_indev.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "tusb_desc.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"

static const char *TAG = "CONN_UI";

#define IDX_WIFI                       0
#define IDX_NETWORKS                   1
#define IDX_USB_NATIVE                 2
#define NAV_TIMER_INTERVAL_MS          50
#define WIFI_LOADING_TIMER_INTERVAL_MS 100
#define WIFI_LOADING_MIN_US            1500000
#define WIFI_LOADING_MAX_US            5000000
#define MSGBOX_DEBOUNCE_US             300000

static lv_obj_t *s_screen_conn = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_wifi_loading_timer = NULL;
static int64_t s_wifi_loading_start_time = 0;
static int64_t s_msgbox_open_time = 0;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void wifi_loading_timer_cb(lv_timer_t *timer);
static void show_wifi_loading(void);
static void update_wifi_toggle(void);
static void nav_timer_cb(lv_timer_t *timer);

void ui_connection_settings_open(void) {
  if (s_screen_conn != NULL) {
    lv_obj_del(s_screen_conn);
    s_screen_conn = NULL;
  }

  bool is_wifi_active = true;

  s_screen_conn = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_conn, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen_conn, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen_conn, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen_conn, "CONNECTION", "/assets/icons/hub.bin");
  menu_component_add_toggle(&s_menu, "/assets/icons/wifi.bin", "WI-FI", is_wifi_active);
  menu_component_add_item(&s_menu, "/assets/icons/wifi_find.bin", "NETWORKS");
  // USB-C data mux: OFF = UART bridge (serial/flash, default), ON = native P4 USB.
  menu_component_add_toggle(&s_menu, "/assets/icons/usb.bin", "USB NATIVE", usb_mux_is_native());

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load(s_screen_conn);
}

static void wifi_loading_timer_cb(lv_timer_t *timer) {
  int64_t elapsed = esp_timer_get_time() - s_wifi_loading_start_time;
  bool is_ready = (wifi_service_is_active() && elapsed >= WIFI_LOADING_MIN_US) ||
                  elapsed >= WIFI_LOADING_MAX_US;
  if (!is_ready)
    return;

  lv_timer_del(timer);
  s_wifi_loading_timer = NULL;
  msgbox_close();
  notify(NOTIFY_INFO, "Wi-Fi on");
}

static void show_wifi_loading(void) {
  if (s_wifi_loading_timer != NULL)
    lv_timer_del(s_wifi_loading_timer);

  s_wifi_loading_start_time = esp_timer_get_time();
  msgbox_open(LV_SYMBOL_WIFI, "LIGANDO WIFI...", NULL, NULL, NULL);
  s_wifi_loading_timer =
      lv_timer_create(wifi_loading_timer_cb, WIFI_LOADING_TIMER_INTERVAL_MS, NULL);
}

static void update_wifi_toggle(void) {
  bool is_active = wifi_service_is_active();
  menu_component_set_toggle(&s_menu, IDX_WIFI, is_active);
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen_conn) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }

  if (ui_input_is_locked())
    return;

  bool is_up = ui_btn_up();
  bool is_down = ui_btn_down();
  bool is_left = ui_btn_left();
  bool is_right = ui_btn_right();
  bool is_ok = ok_button_is_down();
  bool is_back = back_button_is_down();

  int sel = menu_component_get_selected(&s_menu);

  if (is_down && !s_btn_down_last)
    menu_component_next(&s_menu);

  if (is_up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if (is_back && !s_btn_back_last)
    ui_switch_screen(SCREEN_SETTINGS);

  if ((is_left && !s_btn_left_last) || (is_right && !s_btn_right_last)) {
    if (sel == IDX_WIFI) {
      menu_component_toggle_item(&s_menu, IDX_WIFI);
      bool is_new_state = menu_component_get_toggle(&s_menu, IDX_WIFI);
      wifi_service_set_enabled(is_new_state);

      if (is_new_state) {
        show_wifi_loading();
      } else {
        msgbox_close();
        notify(NOTIFY_INFO, "Wi-Fi off");
      }
    } else if (sel == IDX_USB_NATIVE) {
      menu_component_toggle_item(&s_menu, IDX_USB_NATIVE);
      bool native = menu_component_get_toggle(&s_menu, IDX_USB_NATIVE);
      usb_mux_set_native(native);
      notify(NOTIFY_INFO, native ? "USB: native (serial off)" : "USB: UART bridge");
    }
  }

  if ((is_ok && !s_btn_ok_last) || (is_right && !s_btn_right_last)) {
    if (sel == IDX_NETWORKS)
      ui_switch_screen(SCREEN_CONNECT_WIFI);
  }

  s_btn_up_last = is_up;
  s_btn_down_last = is_down;
  s_btn_left_last = is_left;
  s_btn_right_last = is_right;
  s_btn_ok_last = is_ok;
  s_btn_back_last = is_back;
}

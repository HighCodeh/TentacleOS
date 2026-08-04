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

#include "gatt_explorer_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50
#define LABEL_LEN    40

#define GATT_ICON    "/assets/icons/hub.bin"
#define SERVICE_ICON "/assets/icons/folder.bin"
#define CHAR_ICON    "/assets/icons/key.bin"

typedef struct {
  const char *name;
  const char *uuid;
  const char *props;
} gatt_char_t;

typedef struct {
  const char *name;
  const char *uuid;
  const gatt_char_t *chars;
  int char_count;
} gatt_service_t;

static const gatt_char_t GENERIC_ACCESS_CHARS[] = {
    {"Device Name", "0x2A00", "R/W"},
    {"Appearance", "0x2A01", "R"},
    {"Conn Params", "0x2A04", "R"},
};

static const gatt_char_t DEVICE_INFO_CHARS[] = {
    {"Manufacturer", "0x2A29", "R"},
    {"Model Number", "0x2A24", "R"},
    {"Firmware Rev", "0x2A26", "R"},
    {"Serial Number", "0x2A25", "R"},
};

static const gatt_char_t BATTERY_CHARS[] = {
    {"Battery Level", "0x2A19", "R/N"},
};

static const gatt_char_t HID_CHARS[] = {
    {"HID Info", "0x2A4A", "R"},
    {"Report Map", "0x2A4B", "R"},
    {"HID Report", "0x2A4D", "R/W/N"},
    {"Protocol Mode", "0x2A4E", "R/W"},
};

static const gatt_char_t HEART_RATE_CHARS[] = {
    {"HR Measurement", "0x2A37", "N"},
    {"Body Sensor Loc", "0x2A38", "R"},
    {"HR Control Pt", "0x2A39", "W"},
};

static const gatt_service_t SERVICES[] = {
    {"Generic Access",
     "0x1800",
     GENERIC_ACCESS_CHARS,
     (int)(sizeof(GENERIC_ACCESS_CHARS) / sizeof(GENERIC_ACCESS_CHARS[0]))},
    {"Device Information",
     "0x180A",
     DEVICE_INFO_CHARS,
     (int)(sizeof(DEVICE_INFO_CHARS) / sizeof(DEVICE_INFO_CHARS[0]))},
    {"Battery Service",
     "0x180F",
     BATTERY_CHARS,
     (int)(sizeof(BATTERY_CHARS) / sizeof(BATTERY_CHARS[0]))},
    {"Human Interface Device",
     "0x1812",
     HID_CHARS,
     (int)(sizeof(HID_CHARS) / sizeof(HID_CHARS[0]))},
    {"Heart Rate",
     "0x180D",
     HEART_RATE_CHARS,
     (int)(sizeof(HEART_RATE_CHARS) / sizeof(HEART_RATE_CHARS[0]))},
};
#define SERVICES_COUNT ((int)(sizeof(SERVICES) / sizeof(SERVICES[0])))

typedef enum {
  LEVEL_SERVICES = 0,
  LEVEL_CHARS,
} gatt_level_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static gatt_level_t s_level = LEVEL_SERVICES;
static int s_service = 0;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static void build_screen(void);
static void nav_timer_cb(lv_timer_t *t);

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  char label[LABEL_LEN];

  if (s_level == LEVEL_SERVICES) {
    s_menu = menu_component_create(s_screen, "GATT SERVICES", GATT_ICON);
    for (int i = 0; i < SERVICES_COUNT; i++) {
      snprintf(label, sizeof(label), "%s  %s", SERVICES[i].uuid, SERVICES[i].name);
      menu_component_add_item(&s_menu, SERVICE_ICON, label);
    }
    menu_component_set_hint(&s_menu, "OK Open   BACK Exit");
  } else {
    const gatt_service_t *svc = &SERVICES[s_service];
    s_menu = menu_component_create(s_screen, svc->name, SERVICE_ICON);
    for (int i = 0; i < svc->char_count; i++) {
      snprintf(label,
               sizeof(label),
               "%s %s  %s",
               svc->chars[i].uuid,
               svc->chars[i].props,
               svc->chars[i].name);
      menu_component_add_item(&s_menu, CHAR_ICON, label);
    }
    menu_component_set_hint(&s_menu, "OK Read   BACK Up");
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void read_selected_char(int sel) {
  const gatt_service_t *svc = &SERVICES[s_service];
  if (sel < 0 || sel >= svc->char_count)
    return;
  char msg[LABEL_LEN];
  snprintf(msg, sizeof(msg), "%s = 0x%02X", svc->chars[sel].uuid, (unsigned)(esp_random() & 0xFF));
  ui_feedback(UI_FB_READ);
  notify(NOTIFY_INFO, msg);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (ui_input_is_locked()) {
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  bool up_e = up && !s_up_last;
  bool down_e = down && !s_down_last;
  bool left_e = left && !s_left_last;
  bool ok_e = ok && !s_ok_last;
  bool back_e = back && !s_back_last;

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_ok_last = ok;
  s_back_last = back;

  if (down_e) {
    menu_component_next(&s_menu);
    ui_feedback(UI_FB_NAV);
  }
  if (up_e) {
    menu_component_prev(&s_menu);
    ui_feedback(UI_FB_NAV);
  }

  if (ok_e) {
    int sel = menu_component_get_selected(&s_menu);
    if (s_level == LEVEL_SERVICES) {
      if (sel >= 0 && sel < SERVICES_COUNT) {
        s_service = sel;
        s_level = LEVEL_CHARS;
        ui_feedback(UI_FB_SELECT);
        build_screen();
      }
      return;
    }
    read_selected_char(sel);
    return;
  }

  if (back_e || left_e) {
    if (s_level == LEVEL_CHARS) {
      s_level = LEVEL_SERVICES;
      build_screen();
      return;
    }
    ui_switch_screen(SCREEN_BLE_DETECT_MENU);
  }
}

void ui_gatt_explorer_open(void) {
  s_level = LEVEL_SERVICES;
  s_service = 0;
  s_up_last = false;
  s_down_last = false;
  s_left_last = false;
  s_ok_last = false;
  s_back_last = false;
  build_screen();
}

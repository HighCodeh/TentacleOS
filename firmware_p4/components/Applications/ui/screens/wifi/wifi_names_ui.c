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

#include "wifi_names_ui.h"

#include "lvgl.h"

#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_names.h"

#define COLOR_NAME 0x00E676
#define COLOR_ADD  0xCC00FF

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static int s_edit_index = -1;

static void wifi_names_input(const input_event_t *ev, void *ctx);

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "Networks", "/assets/icons/edit_note.bin");

  int count = wifi_names_count();
  for (int i = 0; i < count; i++) {
    menu_component_add_item(&s_menu, "/assets/icons/wifi.bin", wifi_names_get(i));
    menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_NAME));
  }

  if (count < WIFI_NAMES_MAX) {
    menu_component_add_item(&s_menu, "/assets/icons/add.bin", "+ Add network");
    menu_component_set_item_label_color(&s_menu, count, lv_color_hex(COLOR_ADD));
  }

  ui_input_set_screen_handler(wifi_names_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void rebuild_async(void *unused) {
  (void)unused;
  if (ui_current_screen() == SCREEN_WIFI_NAMES)
    build_screen();
}

static void on_kb_submit(const char *text, void *ud) {
  (void)ud;
  int count = wifi_names_count();
  if (s_edit_index == count) {
    wifi_names_add(text);
  } else if (s_edit_index >= 0 && s_edit_index < count) {
    if (text && text[0])
      wifi_names_set(s_edit_index, text);
    else
      wifi_names_remove(s_edit_index);
  }
  s_edit_index = -1;
  ui_async_call(rebuild_async, NULL);
}

static void wifi_names_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_WIFI_MENU);
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        int count = wifi_names_count();
        if (sel >= 0 && sel <= count && !(sel == count && count >= WIFI_NAMES_MAX)) {
          s_edit_index = sel;
          keyboard_open(NULL, on_kb_submit, NULL);
        }
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    default:
      break;
  }
}

void ui_wifi_names_open(void) {
  wifi_names_init();
  s_edit_index = -1;
  build_screen();
}

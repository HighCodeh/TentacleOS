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

// Apps launcher (list view): lists the signed .hb bundles in /sdcard/apps and
// launches the selected one. Scanning, icon/title decode, signature verify and
// the capability-consent flow all live in app_registry (shared with the Apps
// carousel); this screen is just the menu_component view over that.

#include "apps_ui.h"

#include "lvgl.h"

#include "app_registry.h"
#include "input_manager.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define APP_ICON "/assets/icons/description.bin"

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static app_reg_entry_t s_reg[APP_REG_MAX];
static int s_count = 0;

static void launch_selected(void) {
  int sel = menu_component_get_selected(&s_menu);
  if (sel < 0 || sel >= s_count)
    return;
  app_registry_launch(s_reg[sel].file);
}

static void apps_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  bool press = (ev->action == INPUT_ACTION_PRESS);
  bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_OK:
      if (press)
        launch_selected();
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

void ui_apps_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen); // deletes the icon widgets before we free their pixels
    s_screen = NULL;
  }
  app_registry_free(s_reg, s_count);
  s_count = app_registry_scan(s_reg, APP_REG_MAX);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "APPS", NULL);
  if (s_count == 0) {
    menu_component_add_section(&s_menu, "No apps in /sdcard/apps");
  } else {
    for (int i = 0; i < s_count; i++) {
      const char *label = s_reg[i].title[0] ? s_reg[i].title : s_reg[i].file;
      if (s_reg[i].has_icon)
        menu_component_add_item_dsc(&s_menu, &s_reg[i].icon, label);
      else
        menu_component_add_item(&s_menu, APP_ICON, label);
    }
  }
  menu_component_set_hint(&s_menu, "OK run   BACK exit");

  ui_input_set_screen_handler(apps_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "ui_badusb_menu.h"
#include "ui_manager.h"
#include "font/lv_symbol_def.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "lv_port_indev.h"
#include "esp_log.h"

static const char *TAG = "UI_BADUSB_MENU";

static lv_obj_t * screen_badusb_menu = NULL;

static void menu_event_handler(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = lv_event_get_target(e);
  uint32_t * user_data = lv_event_get_user_data(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER) {
      // TODO: Pass user_data to browser to know which storage to open
      ui_switch_screen(SCREEN_BADUSB_BROWSER);
    } else if (key == LV_KEY_ESC) {
      ui_switch_screen(SCREEN_MENU);
    }
  }
}

void ui_badusb_menu_open(void) {
  if (screen_badusb_menu) lv_obj_del(screen_badusb_menu);

  screen_badusb_menu = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_badusb_menu, lv_color_black(), 0);
  lv_obj_remove_flag(screen_badusb_menu, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(screen_badusb_menu);

  lv_obj_t * list = lv_list_create(screen_badusb_menu);
  lv_obj_set_size(list, 220, 180);
  lv_obj_center(list);

  lv_obj_t * btn;

  btn = lv_list_add_button(list, LV_SYMBOL_USB, "Internal Memory");
  lv_obj_add_event_cb(btn, menu_event_handler, LV_EVENT_KEY, (void*)0);

  btn = lv_list_add_button(list, LV_SYMBOL_SD_CARD, "Micro-SD");
  lv_obj_add_event_cb(btn, menu_event_handler, LV_EVENT_KEY, (void*)1);

  footer_ui_create(screen_badusb_menu);

  lv_obj_add_event_cb(screen_badusb_menu, menu_event_handler, LV_EVENT_KEY, NULL);

  if (main_group) {
    lv_group_add_obj(main_group, list);
    lv_group_focus_obj(list);
  }

  lv_screen_load(screen_badusb_menu);
}

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


#include "ui_badusb_layout.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "ducky_parser.h"
#include "bad_usb.h"

static const char *TAG = "UI_BADUSB_LAYOUT";

static lv_obj_t * screen_layout = NULL;

static void layout_event_handler(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = lv_event_get_target(e);
  ducky_layout_t layout = (ducky_layout_t)(intptr_t)lv_event_get_user_data(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER) {
      ESP_LOGI(TAG, "Selected Layout: %d", layout);
      ducky_set_layout(layout);

      bad_usb_init();

      ui_switch_screen(SCREEN_BADUSB_CONNECT);
    } else if (key == LV_KEY_ESC) {
      ui_switch_screen(SCREEN_BADUSB_BROWSER);
    }
  }
}

void ui_badusb_layout_open(void) {
  if (screen_layout) lv_obj_del(screen_layout);

  screen_layout = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_layout, lv_color_black(), 0);
  lv_obj_remove_flag(screen_layout, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(screen_layout);

  lv_obj_t * lbl = lv_label_create(screen_layout);
  lv_label_set_text(lbl, "Select Keyboard Layout:");
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t * list = lv_list_create(screen_layout);
  lv_obj_set_size(list, 200, 120);
  lv_obj_center(list);
  lv_obj_set_style_bg_color(list, lv_color_black(), 0);
  lv_obj_set_style_text_color(list, lv_color_white(), 0);
  lv_obj_set_style_border_color(list, lv_palette_main(LV_PALETTE_DEEP_PURPLE), 0);
  lv_obj_set_style_border_width(list, 2, 0);

  lv_obj_t * btn;

  btn = lv_list_add_button(list, LV_SYMBOL_KEYBOARD, "US (Standard)");
  lv_obj_add_event_cb(btn, layout_event_handler, LV_EVENT_KEY, (void*)(intptr_t)DUCKY_LAYOUT_US);
  lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
  lv_obj_set_style_text_color(btn, lv_color_white(), 0);

  btn = lv_list_add_button(list, LV_SYMBOL_KEYBOARD, "ABNT2 (Brazilian)");
  lv_obj_add_event_cb(btn, layout_event_handler, LV_EVENT_KEY, (void*)(intptr_t)DUCKY_LAYOUT_ABNT2);
  lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
  lv_obj_set_style_text_color(btn, lv_color_white(), 0);

  footer_ui_create(screen_layout);

  lv_obj_add_event_cb(screen_layout, layout_event_handler, LV_EVENT_KEY, NULL);

  if (main_group) {
    lv_group_add_obj(main_group, list);
    lv_group_focus_obj(list);
  }

  lv_screen_load(screen_layout);
}

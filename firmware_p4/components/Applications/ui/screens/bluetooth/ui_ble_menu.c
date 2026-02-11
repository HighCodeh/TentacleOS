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


#include "ui_ble_menu.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "font/lv_symbol_def.h"

#define BG_COLOR            lv_color_black()
#define COLOR_BORDER        0x834EC6
#define COLOR_GRADIENT_TOP  0x000000
#define COLOR_GRADIENT_BOT  0x2E0157

static const char *TAG = "UI_BLE_MENU";

static lv_obj_t * screen_ble_menu = NULL;
static lv_style_t style_menu;
static lv_style_t style_btn;
static bool styles_initialized = false;

static void init_styles(void)
{
  if(styles_initialized) return;

  lv_style_init(&style_menu);
  lv_style_set_bg_opa(&style_menu, LV_OPA_TRANSP);
  lv_style_set_border_width(&style_menu, 2);
  lv_style_set_border_color(&style_menu, lv_color_hex(COLOR_BORDER));
  lv_style_set_radius(&style_menu, 6);
  lv_style_set_pad_all(&style_menu, 10);
  lv_style_set_pad_row(&style_menu, 10);

  lv_style_init(&style_btn);
  lv_style_set_bg_color(&style_btn, lv_color_hex(COLOR_GRADIENT_BOT));
  lv_style_set_bg_grad_color(&style_btn, lv_color_hex(COLOR_GRADIENT_TOP));
  lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
  lv_style_set_border_width(&style_btn, 2);
  lv_style_set_border_color(&style_btn, lv_color_hex(COLOR_BORDER));
  lv_style_set_radius(&style_btn, 6);

  styles_initialized = true;
}

static void menu_item_event_cb(lv_event_t * e)
{
  lv_obj_t * img_sel = lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_FOCUSED) {
    lv_obj_clear_flag(img_sel, LV_OBJ_FLAG_HIDDEN);
  }
  else if(code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(img_sel, LV_OBJ_FLAG_HIDDEN);
  }
}

static void ble_menu_event_cb(lv_event_t * e)
{
  if(lv_event_get_code(e) == LV_EVENT_KEY) {
    if(lv_event_get_key(e) == LV_KEY_ESC || lv_event_get_key(e) == LV_KEY_LEFT) {
      ESP_LOGI(TAG, "Returning to Main Menu");
      ui_switch_screen(SCREEN_MENU);
    }
  }
}

static void option_select_event_cb(lv_event_t * e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY) return;

  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ENTER) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    
    switch(index) {
        case 0: // Device Spam
            ESP_LOGI(TAG, "Opening Device Spam List");
            ui_switch_screen(SCREEN_BLE_SPAM_SELECT);
            break;
        case 1: // Detect Devices
            ESP_LOGI(TAG, "Detect Devices (TODO)");
            break;
        case 2: // Beacon Spam
            ESP_LOGI(TAG, "Beacon Spam (TODO)");
            break;
        default:
            break;
    }
  }
}

static void create_menu(lv_obj_t * parent)
{
  init_styles();

  lv_coord_t menu_h = 240 - 24 - 20;

  lv_obj_t * menu = lv_obj_create(parent);
  lv_obj_set_size(menu, 240, menu_h);
  lv_obj_align(menu, LV_ALIGN_CENTER, 0, 2);
  lv_obj_add_style(menu, &style_menu, 0);
  lv_obj_set_scroll_dir(menu, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);

  static const void * ble_icon = NULL;
  static const void * select_icon = NULL;

  if(!ble_icon) ble_icon = LV_SYMBOL_BLUETOOTH; 
  if(!select_icon) select_icon = LV_SYMBOL_RIGHT;

  const char * options[] = {"Device Spam", "Detect Devices", "Beacon Spam"};
  int count = 3;

  for(int i = 0; i < count; i++) {
    lv_obj_t * btn = lv_btn_create(menu);
    lv_obj_set_size(btn, lv_pct(100), 40);
    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_set_style_anim_time(btn, 0, 0);

    lv_obj_t * img_left = lv_label_create(btn);
    lv_label_set_text(img_left, ble_icon);
    lv_obj_align(img_left, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, options[i]);
    lv_obj_center(lbl);

    lv_obj_t * img_sel = lv_label_create(btn);
    lv_label_set_text(img_sel, select_icon);
    lv_obj_align(img_sel, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_flag(img_sel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(btn, menu_item_event_cb, LV_EVENT_FOCUSED, img_sel);
    lv_obj_add_event_cb(btn, menu_item_event_cb, LV_EVENT_DEFOCUSED, img_sel);

    // Pass index as user data
    lv_obj_add_event_cb(btn, option_select_event_cb, LV_EVENT_KEY, (void*)(intptr_t)i);
    // Add back handler to buttons too
    lv_obj_add_event_cb(btn, ble_menu_event_cb, LV_EVENT_KEY, NULL);

    if(main_group) {
      lv_group_add_obj(main_group, btn);
    }
  }
}

void ui_ble_menu_open(void)
{
  if(screen_ble_menu) {
    lv_obj_del(screen_ble_menu);
    screen_ble_menu = NULL;
  }

  screen_ble_menu = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_ble_menu, BG_COLOR, 0);
  lv_obj_remove_flag(screen_ble_menu, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(screen_ble_menu);
  footer_ui_create(screen_ble_menu);
  create_menu(screen_ble_menu);

  lv_obj_add_event_cb(screen_ble_menu, ble_menu_event_cb, LV_EVENT_KEY, NULL);

  lv_screen_load(screen_ble_menu);
}
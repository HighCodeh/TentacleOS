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


#include "ui_badusb_browser.h"
#include "ui_manager.h"
#include "ui_badusb_running.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "storage_assets.h" // For internal memory
#include <dirent.h>
#include <sys/types.h>

static const char *TAG = "UI_BADUSB_BROWSER";
static lv_obj_t * screen_browser = NULL;

static void file_select_event_handler(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = lv_event_get_target(e);

  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER) {
      const char * filename = lv_list_get_button_text(lv_obj_get_parent(obj), obj);
      ESP_LOGI(TAG, "Selected script: %s", filename);
      ui_badusb_running_set_script(filename);
      ui_switch_screen(SCREEN_BADUSB_LAYOUT);
    } else if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
      ui_switch_screen(SCREEN_BADUSB_MENU);
    }
  }
}

void ui_badusb_browser_open(void) {
  if (screen_browser) lv_obj_del(screen_browser);

  screen_browser = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_browser, lv_color_black(), 0);
  lv_obj_remove_flag(screen_browser, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(screen_browser);

  lv_obj_t * list = lv_list_create(screen_browser);
  lv_obj_set_size(list, 220, 180);
  lv_obj_center(list);
  lv_obj_set_style_bg_color(list, lv_color_black(), 0);
  lv_obj_set_style_text_color(list, lv_color_white(), 0);
  lv_obj_set_style_border_color(list, lv_palette_main(LV_PALETTE_DEEP_PURPLE), 0);
  lv_obj_set_style_border_width(list, 2, 0);

  DIR* dir = opendir("/assets/storage/bad_usb_scripts/");
  if (dir) {
    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
      if (de->d_type == DT_REG) { // Only list files
        lv_obj_t* btn = lv_list_add_button(list, LV_SYMBOL_FILE, de->d_name);
        lv_obj_add_event_cb(btn, file_select_event_handler, LV_EVENT_KEY, NULL);
        lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
        lv_obj_set_style_text_color(btn, lv_color_white(), 0);
      }
    }
    closedir(dir);
  } else {
    lv_obj_t* btn = lv_list_add_button(list, LV_SYMBOL_WARNING, "Directory not found");
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
  }

  footer_ui_create(screen_browser);

  lv_obj_add_event_cb(screen_browser, file_select_event_handler, LV_EVENT_KEY, NULL);

  if (main_group) {
    lv_group_add_obj(main_group, list);
    lv_group_focus_obj(list);
  }

  lv_screen_load(screen_browser);
}


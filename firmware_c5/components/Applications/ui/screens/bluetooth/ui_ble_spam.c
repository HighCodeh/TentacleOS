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


#include "ui_ble_spam.h"
#include "canned_spam.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include <stdio.h>

#define BG_COLOR lv_color_black()

static lv_obj_t * screen_spam = NULL;
static char current_spam_name[32] = "Unknown";

void ui_ble_spam_set_name(const char *name) {
  if (name) {
    snprintf(current_spam_name, sizeof(current_spam_name), "%s", name);
  }
}

static void spam_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if(key == LV_KEY_ESC || key == LV_KEY_LEFT) {
      // Stop spam and go back to BLE Menu
      spam_stop();
      ui_switch_screen(SCREEN_BLE_MENU);
    }
  }
}

void ui_ble_spam_open(void) {
  if(screen_spam) {
    lv_obj_del(screen_spam);
    screen_spam = NULL;
  }

  screen_spam = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_spam, BG_COLOR, 0);
  lv_obj_remove_flag(screen_spam, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(screen_spam);
  footer_ui_create(screen_spam);

  lv_obj_t * lbl_title = lv_label_create(screen_spam);
  lv_label_set_text_fmt(lbl_title, "SPAM RUNNING:\n#FF0000 %s#", current_spam_name);
  lv_label_set_recolor(lbl_title, true);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
  lv_obj_center(lbl_title);
  lv_obj_set_y(lbl_title, -20);

  lv_obj_t * lbl_instr = lv_label_create(screen_spam);
  lv_label_set_text(lbl_instr, "Press BACK to Stop");
  lv_obj_set_style_text_color(lbl_instr, lv_color_white(), 0);
  lv_obj_align(lbl_instr, LV_ALIGN_CENTER, 0, 40);

  lv_obj_t * spinner = lv_spinner_create(screen_spam);
  lv_obj_set_size(spinner, 15, 15);
  lv_obj_align(spinner, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0xFF0000), LV_PART_INDICATOR);

  lv_obj_add_event_cb(screen_spam, spam_event_cb, LV_EVENT_KEY, NULL);

  if (main_group) {
    lv_group_add_obj(main_group, screen_spam);
    lv_group_focus_obj(screen_spam);
  }

  lv_screen_load(screen_spam);
}

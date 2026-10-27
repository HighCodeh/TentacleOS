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

#include "ble_spam_names_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "esp_log.h"

#include "bluetooth_service.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "BLE_SPAM_NAMES_UI";

#define NAMES_MAX 10
#define NAME_LEN  24

#define NAME_ICON "/assets/icons/broadcast_on_personal.bin"
#define ADD_ICON  "/assets/icons/add.bin"
#define HEAD_ICON "/assets/icons/edit_note.bin"

#define COLOR_NAME 0x00E676
#define COLOR_ADD  0xCC00FF

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static char s_names[NAMES_MAX][NAME_LEN];
static int s_count = 0;
static int s_del_index = -1;

static void ble_spam_names_input(const input_event_t *ev, void *ctx);

static void load_names(void) {
  s_count = 0;
  char **list = NULL;
  size_t count = 0;
  if (bluetooth_service_load_spam_list(&list, &count) != ESP_OK || list == NULL)
    return;
  for (size_t i = 0; i < count && s_count < NAMES_MAX; i++) {
    if (list[i] != NULL) {
      snprintf(s_names[s_count], NAME_LEN, "%s", list[i]);
      s_count++;
    }
  }
  bluetooth_service_free_spam_list(list, count);
}

static void save_names(void) {
  const char *ptrs[NAMES_MAX];
  for (int i = 0; i < s_count; i++)
    ptrs[i] = s_names[i];
  esp_err_t err = bluetooth_service_save_spam_list(ptrs, (size_t)s_count);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "save spam list failed: %s", esp_err_to_name(err));
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "SPAM NAMES", HEAD_ICON);

  for (int i = 0; i < s_count; i++) {
    menu_component_add_item(&s_menu, NAME_ICON, s_names[i]);
    menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_NAME));
  }

  if (s_count < NAMES_MAX) {
    menu_component_add_item(&s_menu, ADD_ICON, "+ Add name");
    menu_component_set_item_label_color(&s_menu, s_count, lv_color_hex(COLOR_ADD));
  }

  menu_component_set_hint(&s_menu, "OK add / delete   BACK back");

  ui_input_set_screen_handler(ble_spam_names_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void rebuild_async(void *unused) {
  (void)unused;
  if (ui_current_screen() == SCREEN_BLE_SPAM_NAMES)
    build_screen();
}

static void on_kb_submit(const char *text, void *ud) {
  (void)ud;
  if (text != NULL && text[0] != '\0' && s_count < NAMES_MAX) {
    snprintf(s_names[s_count], NAME_LEN, "%s", text);
    s_count++;
    save_names();
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_SAVED, "Name added");
  }
  ui_async_call(rebuild_async, NULL);
}

static void on_delete_confirm(bool confirm) {
  if (confirm && s_del_index >= 0 && s_del_index < s_count) {
    for (int i = s_del_index; i < s_count - 1; i++)
      memmove(s_names[i], s_names[i + 1], NAME_LEN);
    s_count--;
    save_names();
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_INFO, "Name deleted");
  }
  s_del_index = -1;
  ui_async_call(rebuild_async, NULL);
}

static void ble_spam_names_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_BLE_SPAM_SELECT);
      break;
    case INPUT_BTN_OK:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < s_count) {
          s_del_index = sel;
          msgbox_open(LV_SYMBOL_TRASH, "Delete this name?", "Delete", "Cancel", on_delete_confirm);
        } else if (sel == s_count && s_count < NAMES_MAX) {
          keyboard_open(NULL, on_kb_submit, NULL);
        }
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        menu_component_next(&s_menu);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        menu_component_prev(&s_menu);
        ui_feedback(UI_FB_NAV);
      }
      break;
    default:
      break;
  }
}

void ui_ble_spam_names_open(void) {
  load_names();
  s_del_index = -1;
  build_screen();
}

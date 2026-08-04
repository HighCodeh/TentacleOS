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

#include "buttons_gpio.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50
#define NAMES_MAX    10
#define NAME_LEN     24

#define NAME_ICON "/assets/icons/broadcast_on_personal.bin"
#define ADD_ICON  "/assets/icons/add.bin"
#define HEAD_ICON "/assets/icons/edit_note.bin"

#define COLOR_NAME 0x00E676
#define COLOR_ADD  0xCC00FF

static const char *const SEED_NAMES[] = {
    "AirPods Pro",
    "Nintendo Switch",
    "TV Assistant",
    "Ray-Ban Meta",
};
#define SEED_NAMES_COUNT ((int)(sizeof(SEED_NAMES) / sizeof(SEED_NAMES[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static char s_names[NAMES_MAX][NAME_LEN];
static int s_count = 0;
static bool s_seeded = false;
static int s_del_index = -1;

static bool s_up_last, s_down_last, s_left_last, s_ok_last, s_back_last;

static void nav_timer_cb(lv_timer_t *t);

static void seed_once(void) {
  if (s_seeded)
    return;
  s_seeded = true;
  s_count = SEED_NAMES_COUNT;
  for (int i = 0; i < s_count; i++)
    snprintf(s_names[i], NAME_LEN, "%s", SEED_NAMES[i]);
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

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
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
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_SAVED, "Name added");
  }
  lv_async_call(rebuild_async, NULL);
}

static void on_delete_confirm(bool confirm) {
  if (confirm && s_del_index >= 0 && s_del_index < s_count) {
    for (int i = s_del_index; i < s_count - 1; i++)
      memmove(s_names[i], s_names[i + 1], NAME_LEN);
    s_count--;
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_INFO, "Name deleted");
  }
  s_del_index = -1;
  lv_async_call(rebuild_async, NULL);
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

  if (keyboard_is_open() || msgbox_is_open() || ui_input_is_locked()) {
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  if (down && !s_down_last) {
    menu_component_next(&s_menu);
    ui_feedback(UI_FB_NAV);
  }
  if (up && !s_up_last) {
    menu_component_prev(&s_menu);
    ui_feedback(UI_FB_NAV);
  }

  if ((back && !s_back_last) || (left && !s_left_last))
    ui_switch_screen(SCREEN_BLE_SPAM_SELECT);

  if (ok && !s_ok_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < s_count) {
      s_del_index = sel;
      msgbox_open(LV_SYMBOL_TRASH, "Delete this name?", "Delete", "Cancel", on_delete_confirm);
    } else if (sel == s_count && s_count < NAMES_MAX) {
      keyboard_open(NULL, on_kb_submit, NULL);
    }
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_ble_spam_names_open(void) {
  seed_once();
  s_del_index = -1;
  s_up_last = s_down_last = s_left_last = s_ok_last = s_back_last = false;
  build_screen();
}

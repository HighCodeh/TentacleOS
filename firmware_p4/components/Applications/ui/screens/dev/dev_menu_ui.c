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

#include "dev_menu_ui.h"

#include "lvgl.h"

#include "menu_component_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define FADE_MS      200

#define TITLE       "DEVELOPER"
#define TITLE_ICON  "/assets/icons/developer_board.bin"
#define FOOTER_HINT "UP/DOWN  OK select  BACK"

static const struct {
  const char *name;
  const char *icon;
  screen_id_t screen;
} MENU_ITEMS[] = {
    {"Scripts", "/assets/icons/description.bin", SCREEN_SCRIPTS},
    {"Console", "/assets/icons/monitoring.bin", SCREEN_DEV_CONSOLE},
    {"P4 Update", "/assets/icons/system_update.bin", SCREEN_SYSTEM_UPDATE},
    {"Diagnostics", "/assets/icons/troubleshoot.bin", SCREEN_DEV_DIAG},
};
#define MENU_ITEM_COUNT ((int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static void dev_menu_input(const input_event_t *ev, void *ctx);

static void opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fade_in(lv_obj_t *obj, uint32_t duration_ms) {
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, duration_ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
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
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  s_menu = menu_component_create(s_screen, TITLE, TITLE_ICON);
  for (int i = 0; i < MENU_ITEM_COUNT; i++)
    menu_component_add_item(&s_menu, MENU_ITEMS[i].icon, MENU_ITEMS[i].name);
  menu_component_set_hint(&s_menu, FOOTER_HINT);

  fade_in(s_menu.items_cont, FADE_MS);
  fade_in(s_menu.title_bar, FADE_MS);

  ui_input_set_screen_handler(dev_menu_input, NULL);

  ui_screen_load(s_screen);
}

static void dev_menu_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_OK:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < MENU_ITEM_COUNT) {
          ui_feedback(UI_FB_SELECT);
          ui_switch_screen(MENU_ITEMS[sel].screen);
        }
      }
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    default:
      break;
  }
}

void ui_dev_menu_open(void) {
  build_screen();
}

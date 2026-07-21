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

#include "ir_saved_ui.h"

#include "esp_log.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_SAVED_UI";

#define NAV_TIMER_MS 50
#define SIG_GREEN    0x00E676

static const char *MOCK_PROTOCOLS[] = {"NEC", "SAMSUNG", "RC5", "SONY"};
#define MOCK_PROTOCOLS_COUNT ((int)(sizeof(MOCK_PROTOCOLS) / sizeof(MOCK_PROTOCOLS[0])))

static const char *MOCK_FILES[] = {"power", "vol_up", "vol_down", "mute", "source"};
#define MOCK_FILES_COUNT ((int)(sizeof(MOCK_FILES) / sizeof(MOCK_FILES[0])))

typedef enum {
  LEVEL_PROTOCOLS = 0,
  LEVEL_FILES,
} browse_level_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static browse_level_t s_level = LEVEL_PROTOCOLS;
static int s_proto = 0;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void build_screen(void);
static void nav_timer_cb(lv_timer_t *t);

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  if (s_level == LEVEL_PROTOCOLS) {
    s_menu =
        menu_component_create(s_screen, "BROWSE SIGNALS", "/assets/icons/search_menu_icon.bin");
    for (int i = 0; i < MOCK_PROTOCOLS_COUNT; i++)
      menu_component_add_item(&s_menu, NULL, MOCK_PROTOCOLS[i]);
  } else {
    s_menu = menu_component_create(s_screen, MOCK_PROTOCOLS[s_proto], NULL);
    for (int i = 0; i < MOCK_FILES_COUNT; i++)
      menu_component_add_item(&s_menu, NULL, MOCK_FILES[i]);
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if (ok && !s_btn_ok_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (s_level == LEVEL_PROTOCOLS) {
      s_proto = sel;
      s_level = LEVEL_FILES;
      build_screen();
      return;
    }
    ESP_LOGI(TAG, "mock open: %s/%s", MOCK_PROTOCOLS[s_proto], MOCK_FILES[sel]);
    menu_component_set_item_label_color(&s_menu, sel, lv_color_hex(SIG_GREEN));
  }

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last)) {
    if (s_level == LEVEL_FILES) {
      s_level = LEVEL_PROTOCOLS;
      build_screen();
      return;
    }
    ui_switch_screen(SCREEN_IR_MENU);
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_ir_saved_open(void) {
  s_level = LEVEL_PROTOCOLS;
  s_proto = 0;
  build_screen();
}

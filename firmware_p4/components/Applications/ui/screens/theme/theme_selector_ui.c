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

#include "theme_selector_ui.h"

#include "esp_log.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "THEME_SELECTOR_UI";

#define NAV_TIMER_MS  50
#define ENTRY_FADE_MS 200
#define ACTIVE_COLOR  0x00E676
#define TITLE_ICON    "/assets/icons/theme_menu_icon.bin"

extern int theme_idx;

static const char *const THEME_LABELS[] = {
    "Default",
    "Matrix",
    "Cyber Blue",
    "Blood",
    "Toxic",
    "Ghost",
    "Neon Pink",
    "Amber",
    "Terminal",
    "Ice",
    "Deep Purple",
    "Midnight",
};
#define THEME_COUNT ((int)(sizeof(THEME_LABELS) / sizeof(THEME_LABELS[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static int s_sel = 0;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static void build_screen(void);

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
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (down && !s_down_last) {
    menu_component_next(&s_menu);
    s_sel = menu_component_get_selected(&s_menu);
  }
  if (up && !s_up_last) {
    menu_component_prev(&s_menu);
    s_sel = menu_component_get_selected(&s_menu);
  }
  if (ok && !s_ok_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < THEME_COUNT && sel != theme_idx) {
      theme_idx = sel;
      ui_theme_load_idx(sel);
      ESP_LOGI(TAG, "applied theme %d (%s)", sel, THEME_LABELS[sel]);
      s_sel = sel;
      build_screen();
      notify(NOTIFY_SAVED, "Theme applied");
      return;
    }
  }
  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_SETTINGS);
    return;
  }

  s_up_last = up;
  s_down_last = down;
  s_ok_last = ok;
  s_back_last = back;
}

static void build_screen(void) {
  lv_obj_t *prev = s_screen;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "THEME", TITLE_ICON);
  for (int i = 0; i < THEME_COUNT; i++) {
    menu_component_add_item(&s_menu, NULL, THEME_LABELS[i]);
    if (i == theme_idx)
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(ACTIVE_COLOR));
  }

  if (s_sel < 0)
    s_sel = 0;
  if (s_sel >= THEME_COUNT)
    s_sel = THEME_COUNT - 1;
  menu_component_select(&s_menu, s_sel);

  if (s_menu.items_cont != NULL)
    lv_obj_fade_in(s_menu.items_cont, ENTRY_FADE_MS, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
  if (prev != NULL)
    lv_obj_del(prev);
}

void ui_theme_selector_open(void) {
  s_sel = (theme_idx >= 0 && theme_idx < THEME_COUNT) ? theme_idx : 0;
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  build_screen();
}

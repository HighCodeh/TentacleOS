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

#include "about_settings_ui.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "ABOUT_SETTINGS_UI";

#define NAV_TIMER_MS  50
#define BOB_PX        5
#define BOB_MS        1200
#define ENTRY_FADE_MS 220

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;

static bool s_back_last = false;

static void bob_exec_cb(void *obj, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool back = back_button_is_down();
  if (back && !s_back_last)
    ui_switch_screen(SCREEN_SETTINGS);
  s_back_last = back;
}

void ui_about_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  (void)TAG;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "ABOUT", "/assets/icons/about_menu_icon.bin");
  ui_chrome_footer(s_screen, "BACK: EXIT");

  lv_obj_t *col = lv_obj_create(s_screen);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_center(col);
  lv_obj_set_style_translate_y(col, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2, 0);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 6, 0);

  lv_image_dsc_t *octobit = assets_get("/assets/img/octobit.bin");
  if (octobit != NULL) {
    lv_obj_t *img = lv_image_create(col);
    lv_image_set_src(img, octobit);

    lv_anim_t bob;
    lv_anim_init(&bob);
    lv_anim_set_var(&bob, img);
    lv_anim_set_exec_cb(&bob, bob_exec_cb);
    lv_anim_set_values(&bob, -BOB_PX, BOB_PX);
    lv_anim_set_duration(&bob, BOB_MS);
    lv_anim_set_playback_duration(&bob, BOB_MS);
    lv_anim_set_repeat_count(&bob, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&bob, lv_anim_path_ease_in_out);
    lv_anim_start(&bob);
  }

  lv_obj_t *name = lv_label_create(col);
  lv_label_set_text(name, "HighBoy V2");
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);

  lv_obj_t *fw = lv_label_create(col);
  lv_label_set_text(fw, "FW 2.0.0 (4180701)");
  lv_obj_set_style_text_font(fw, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(fw, current_theme.border_accent, 0);

  lv_obj_t *chip = lv_label_create(col);
  lv_label_set_text(chip, "ESP32-P4");
  lv_obj_set_style_text_font(chip, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(chip, current_theme.text_main, 0);

  lv_obj_t *mac = lv_label_create(col);
  lv_label_set_text(mac, "WiFi MAC AA:BB:CC:DD:EE:FF");
  lv_obj_set_style_text_font(mac, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(mac, current_theme.border_accent, 0);

  lv_obj_t *uptime = lv_label_create(col);
  lv_label_set_text(uptime, "Uptime 00:12:43");
  lv_obj_set_style_text_font(uptime, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(uptime, current_theme.text_main, 0);

  lv_obj_t *copy = lv_label_create(col);
  lv_label_set_text(copy, "(c) 2025 HIGH CODE");
  lv_obj_set_style_text_font(copy, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(copy, current_theme.border_accent, 0);

  lv_obj_fade_in(col, ENTRY_FADE_MS, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

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

#include "home_ui.h"

#include <stdio.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "st7789.h"

#include "assets_manager.h"
#include "core/lv_group.h"
#include "dropdown_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "HOME_UI";

#define HOME_HEADER_HEIGHT_PCT 9
#define HOME_HEADER_HEIGHT     ((LCD_V_RES * HOME_HEADER_HEIGHT_PCT) / 100)
#define HOME_PUSH_ICON_COUNT   4
#define HOME_ROTATION_DOWN     1800
#define HOME_ROTATION_LEFT     2700
#define HOME_ROTATION_RIGHT    900

#define HOME_ART_ASSET "/assets/img/image.bin"
#define HOME_FLOAT_AMP 7
#define HOME_FLOAT_MS  1300

static lv_obj_t *s_screen_home = NULL;

static void home_event_cb(lv_event_t *e);

static void home_float_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

void ui_home_open(void) {
  if (s_screen_home != NULL) {
    lv_obj_del(s_screen_home);
    s_screen_home = NULL;
  }

  s_screen_home = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_home, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen_home, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen_home, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen_home);
  dropdown_ui_create(s_screen_home);

  static lv_image_dsc_t *s_push_dsc = NULL;
  if (s_push_dsc == NULL)
    s_push_dsc = assets_get("/assets/icons/push_icon.bin");

  if (s_push_dsc != NULL) {
    int16_t w = s_push_dsc->header.w;
    int16_t h = s_push_dsc->header.h;

    static lv_obj_t *s_push_icons[HOME_PUSH_ICON_COUNT];

    s_push_icons[0] = lv_image_create(s_screen_home);
    lv_image_set_src(s_push_icons[0], s_push_dsc);
    lv_obj_align(s_push_icons[0], LV_ALIGN_TOP_MID, 0, HOME_HEADER_HEIGHT);

    s_push_icons[1] = lv_image_create(s_screen_home);
    lv_image_set_src(s_push_icons[1], s_push_dsc);
    lv_image_set_pivot(s_push_icons[1], w / 2, h / 2);
    lv_image_set_rotation(s_push_icons[1], HOME_ROTATION_DOWN);
    lv_obj_align(s_push_icons[1], LV_ALIGN_BOTTOM_MID, 0, 0);

    s_push_icons[2] = lv_image_create(s_screen_home);
    lv_image_set_src(s_push_icons[2], s_push_dsc);
    lv_image_set_pivot(s_push_icons[2], w / 2, h / 2);
    lv_image_set_rotation(s_push_icons[2], HOME_ROTATION_LEFT);
    lv_obj_align(s_push_icons[2], LV_ALIGN_LEFT_MID, -h / 2, 0);

    s_push_icons[3] = lv_image_create(s_screen_home);
    lv_image_set_src(s_push_icons[3], s_push_dsc);
    lv_image_set_pivot(s_push_icons[3], w / 2, h / 2);
    lv_image_set_rotation(s_push_icons[3], HOME_ROTATION_RIGHT);
    lv_obj_align(s_push_icons[3], LV_ALIGN_RIGHT_MID, h / 2, 0);

    dropdown_ui_register_hide_objs(s_push_icons, HOME_PUSH_ICON_COUNT);
  } else {
    static const struct {
      const char *sym;
      int dx;
      int dy;
    } dirs[HOME_PUSH_ICON_COUNT] = {
        {LV_SYMBOL_UP, 0, -64},
        {LV_SYMBOL_DOWN, 0, 64},
        {LV_SYMBOL_LEFT, -64, 0},
        {LV_SYMBOL_RIGHT, 64, 0},
    };

    for (int i = 0; i < HOME_PUSH_ICON_COUNT; i++) {
      lv_obj_t *arrow = lv_label_create(s_screen_home);
      lv_label_set_text(arrow, dirs[i].sym);
      lv_obj_set_style_text_color(arrow, current_theme.border_accent, 0);
      lv_obj_set_style_text_font(arrow, &lv_font_montserrat_14, 0);
      lv_obj_align(arrow, LV_ALIGN_CENTER, dirs[i].dx, dirs[i].dy);
    }
  }

  static lv_image_dsc_t *s_art_dsc = NULL;
  if (s_art_dsc == NULL)
    s_art_dsc = assets_get(HOME_ART_ASSET);
  if (s_art_dsc != NULL) {
    lv_obj_t *art = lv_image_create(s_screen_home);
    lv_image_set_src(art, s_art_dsc);

    lv_image_set_pivot(art, s_art_dsc->header.w / 2, s_art_dsc->header.h / 2);
    lv_image_set_scale(art, 373);
    lv_obj_align(art, LV_ALIGN_CENTER, 0, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, art);
    lv_anim_set_exec_cb(&a, home_float_cb);
    lv_anim_set_values(&a, -HOME_FLOAT_AMP, HOME_FLOAT_AMP);
    lv_anim_set_duration(&a, HOME_FLOAT_MS);
    lv_anim_set_playback_duration(&a, HOME_FLOAT_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }

  lv_obj_add_event_cb(s_screen_home, home_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen_home);
    lv_group_focus_obj(s_screen_home);
  }

  ui_screen_load(s_screen_home);
}

static void home_event_cb(lv_event_t *e) {
  (void)e;
  if (dropdown_ui_is_open())
    return;

  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_RIGHT)
      ui_switch_screen(SCREEN_MENU);
    else if (key == LV_KEY_DOWN)
      ui_switch_screen(SCREEN_SETTINGS);
    else if (key == LV_KEY_LEFT)
      ui_switch_screen(SCREEN_OCTOBIT_STATUS);
  }
}

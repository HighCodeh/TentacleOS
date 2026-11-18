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
#include "app_switcher_ui.h"

#include <stdio.h>
#include "esp_log.h"
#include "assets_manager.h"
#include "favorites.h"
#include "menu_ui.h"
#include "sys_time.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define HOME_FAV_MAX 5
#define HOME_GAP     6
#define HOME_PAD     10

static const char *TAG = "HOME_UI";
static lv_obj_t *s_screen_home, *s_date, *s_name;
static lv_obj_t *s_tiles[HOME_FAV_MAX];
static screen_id_t s_targets[HOME_FAV_MAX];
static const char *s_names[HOME_FAV_MAX];
static int s_count, s_focus;
static lv_timer_t *s_clock;
static const char *icon_for_screen(screen_id_t s) {
  switch (s) {
    case SCREEN_WIFI_MENU:
      return "/assets/icons/wifi.bin";
    case SCREEN_BLE_MENU:
      return "/assets/icons/bluetooth.bin";
    case SCREEN_NFC_MENU:
      return "/assets/icons/nfc.bin";
    case SCREEN_RFID_MENU:
      return "/assets/icons/contactless.bin";
    case SCREEN_IR_MENU:
      return "/assets/icons/settings_input_antenna.bin";
    case SCREEN_SUBGHZ_MENU:
      return "/assets/icons/cell_tower.bin";
    case SCREEN_LORA_CHAT:
      return "/assets/icons/router.bin";
    case SCREEN_BADUSB_MENU:
      return "/assets/icons/usb.bin";
    case SCREEN_GPIO:
      return "/assets/icons/developer_board.bin";
    case SCREEN_SETTINGS:
      return "/assets/icons/settings.bin";
    case SCREEN_FILES:
      return "/assets/icons/folder.bin";
    case SCREEN_PLAYER:
      return "/assets/icons/music_note.bin";
    case SCREEN_DEV_MENU:
      return "/assets/icons/developer_board.bin";
    case SCREEN_OCTOBIT_STATUS:
      return "/assets/icons/monitoring.bin";
    default:
      return NULL;
  }
}

static lv_obj_t *group(lv_obj_t *parent, lv_flex_flow_t flow) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_pad_gap(obj, HOME_GAP, 0);
  lv_obj_set_flex_flow(obj, flow);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, lv_color_t color) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(obj, color, 0);
  return obj;
}

static void clock_refresh(lv_timer_t *timer) {
  (void)timer;
  char date[32];
  if (s_date != NULL && sys_time_format(date, sizeof(date), "%a, %d %b %Y"))
    lv_label_set_text(s_date, date);
}

static void focus_refresh(void) {
  for (int i = 0; i < s_count; ++i) {
    bool selected = i == s_focus;
    lv_obj_set_style_border_color(
        s_tiles[i], selected ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(s_tiles[i], selected ? 2 : 1, 0);
    lv_obj_set_style_bg_color(
        s_tiles[i], selected ? current_theme.bg_secondary : current_theme.bg_primary, 0);
  }
  lv_label_set_text(s_name, s_names[s_focus]);
}

static void build_favorites(lv_obj_t *parent, int width) {
  screen_id_t previous = s_count > 0 ? s_targets[s_focus] : SCREEN_NONE;
  s_count = 0;
  for (int i = 0; i < menu_catalog_count() && s_count < HOME_FAV_MAX - 1; ++i) {
    if (favorites_is(menu_catalog_target(i))) {
      s_targets[s_count] = menu_catalog_target(i);
      s_names[s_count++] = menu_catalog_name(i);
    }
  }
  if (s_count == 0) {
    static const screen_id_t defaults[] = {
        SCREEN_NFC_MENU, SCREEN_IR_MENU, SCREEN_PLAYER, SCREEN_SETTINGS};
    static const char *names[] = {"NFC", "Infrared", "Music", "Settings"};
    for (int i = 0; i < 4; ++i) {
      s_targets[s_count] = defaults[i];
      s_names[s_count++] = names[i];
    }
  }
  s_targets[s_count] = SCREEN_OCTOBIT_STATUS;
  s_names[s_count++] = "Octobit";
  s_focus = 0;
  for (int i = 0; i < s_count; ++i)
    if (s_targets[i] == previous)
      s_focus = i;

  label(parent, "SHORTCUTS", current_theme.text_secondary);
  lv_obj_t *row = group(parent, LV_FLEX_FLOW_ROW);
  int size = (width - HOME_GAP * (s_count - 1)) / s_count;
  if (size > 46)
    size = 46;
  lv_obj_set_size(row, width, size);
  for (int i = 0; i < s_count; ++i) {
    lv_obj_t *tile = group(row, LV_FLEX_FLOW_ROW);
    s_tiles[i] = tile;
    lv_obj_set_size(tile, size, size);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    const char *path = icon_for_screen(s_targets[i]);
    const lv_image_dsc_t *icon = path ? assets_get(path) : NULL;
    if (icon != NULL) {
      lv_obj_t *image = lv_image_create(tile);
      lv_image_set_src(image, icon);
      lv_obj_set_size(image, size - 12, size - 12);
      lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
      lv_obj_set_style_image_recolor(image, current_theme.text_main, 0);
      lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    } else {
      label(tile, LV_SYMBOL_DIRECTORY, current_theme.text_main);
    }
  }
  s_name = label(parent, "", current_theme.text_main);
  lv_obj_set_width(s_name, width);
  lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_name, LV_TEXT_ALIGN_CENTER, 0);
  focus_refresh();
}

static void float_art(void *obj, int32_t y) {
  lv_obj_set_style_translate_y(obj, y, 0);
}

static void build_mascot(lv_obj_t *parent, int width, int height) {
  lv_obj_t *card = group(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(card, width, height);
  lv_obj_set_style_radius(card, 18, 0);
  ui_chrome_gradient(card, current_theme.bg_secondary, current_theme.screen_base, height);
  lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  const lv_image_dsc_t *art = assets_get("/assets/img/image.bin");
  if (art == NULL || art->header.w == 0 || art->header.h == 0) {
    label(card, "HIGH BOY", current_theme.border_accent);
    return;
  }
  lv_obj_t *image = lv_image_create(card);
  lv_image_set_src(image, art);
  lv_obj_set_size(image, width - 16, height - 16);
  lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
  lv_image_set_antialias(image, true);
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, image);
  lv_anim_set_exec_cb(&anim, float_art);
  lv_anim_set_values(&anim, -3, 3);
  lv_anim_set_duration(&anim, 1600);
  lv_anim_set_playback_duration(&anim, 1600);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_start(&anim);
}

static void home_input(const input_event_t *event, void *context) {
  (void)context;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (nav && (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT)) {
    s_focus = (s_focus + s_count + (event->button == INPUT_BTN_RIGHT ? 1 : -1)) % s_count;
    focus_refresh();
    ui_feedback(UI_FB_NAV);
  } else if (press && event->button == INPUT_BTN_OK) {
    ui_switch_screen(s_targets[s_focus]);
  } else if (press && event->button == INPUT_BTN_UP) {
    ui_tasks_toggle();
  } else if (press && event->button == INPUT_BTN_DOWN) {
    ui_switch_screen(SCREEN_MENU);
  }
}

void ui_home_close(void) {
  if (s_clock != NULL) {
    lv_timer_delete(s_clock);
    s_clock = NULL;
  }
  s_date = NULL;
}

void ui_home_open(void) {
  ui_home_close();
  if (s_screen_home != NULL)
    lv_obj_delete(s_screen_home);
  s_screen_home = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_home, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen_home, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_screen_home, 0, 0);
  lv_obj_remove_flag(s_screen_home, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_screen_home, "", NULL);
  ui_chrome_footer(s_screen_home,
                   LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " OK  " LV_SYMBOL_UP " Recent  " LV_SYMBOL_DOWN
                                                  " Apps");
  int width = ui_screen_w() - 2 * HOME_PAD;
  int height = ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H - 2 * HOME_GAP;
  bool wide = ui_screen_w() > ui_screen_h();
  lv_obj_t *body = group(s_screen_home, wide ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_pos(body, HOME_PAD, UI_CHROME_HEADER_H + HOME_GAP);
  lv_obj_set_size(body, width, height);
  if (wide)
    build_mascot(body, 126, height);
  lv_obj_t *controls = wide ? group(body, LV_FLEX_FLOW_COLUMN) : body;
  int control_width = wide ? width - 126 - HOME_GAP : width;
  if (wide)
    lv_obj_set_size(controls, control_width, height);
  s_date = label(controls, "", current_theme.text_secondary);
  lv_obj_set_width(s_date, control_width);
  lv_label_set_long_mode(s_date, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
  clock_refresh(NULL);
  if (!wide)
    build_mascot(controls, width, height - 120);
  build_favorites(controls, control_width);
  s_clock = lv_timer_create(clock_refresh, 30000, NULL);
  if (s_clock == NULL)
    ESP_LOGW(TAG, "Home date refresh unavailable");
  ui_input_set_screen_handler(home_input, NULL);
  ui_screen_load_owned(&s_screen_home, s_screen_home);
}

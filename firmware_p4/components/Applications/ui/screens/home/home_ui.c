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
#include "esp_log.h"
#include "assets_manager.h"
#include "sys_time.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define HOME_GAP              6
#define HOME_PAD              10
#define HOME_DATE_HEIGHT      20
#define HOME_CLOCK_REFRESH_MS 30000

static const char *TAG = "HOME_UI";
static lv_obj_t *s_screen_home, *s_date;
static lv_timer_t *s_clock;

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
  if (event->action == INPUT_ACTION_PRESS &&
      (event->button == INPUT_BTN_OK || event->button == INPUT_BTN_DOWN))
    ui_switch_screen(SCREEN_MENU);
}

static void screen_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen_home)
    ui_home_close();
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
  lv_obj_add_event_cb(s_screen_home, screen_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_set_style_bg_color(s_screen_home, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen_home, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_screen_home, 0, 0);
  lv_obj_remove_flag(s_screen_home, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_screen_home, "", NULL);
  ui_chrome_footer(s_screen_home, "OK  Apps");
  int width = ui_screen_w() - 2 * HOME_PAD;
  int height = ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H - 2 * HOME_GAP;
  lv_obj_t *body = group(s_screen_home, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_pos(body, HOME_PAD, UI_CHROME_HEADER_H + HOME_GAP);
  lv_obj_set_size(body, width, height);
  s_date = label(body, "", current_theme.text_secondary);
  lv_obj_set_size(s_date, width, HOME_DATE_HEIGHT);
  lv_label_set_long_mode(s_date, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
  clock_refresh(NULL);
  build_mascot(body, width, height - HOME_DATE_HEIGHT - HOME_GAP);
  s_clock = lv_timer_create(clock_refresh, HOME_CLOCK_REFRESH_MS, NULL);
  if (s_clock == NULL)
    ESP_LOGW(TAG, "Home date refresh unavailable");
  ui_input_set_screen_handler(home_input, NULL);
  ui_screen_load_owned(&s_screen_home, s_screen_home);
}

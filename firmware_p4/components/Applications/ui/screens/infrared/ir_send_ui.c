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

#include "ir_send_ui.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#include "esp_log.h"

#include "ui_theme.h"
#include "ui_manager.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "buttons_gpio.h"
#include "assets_manager.h"
#include "ir.h"
#include "ir_file.h"
#include "tos_storage_paths.h"
#include "st7789.h"

static const char *TAG = "IR_SEND_UI";

#define OUTER_BORDER 4
#define TOP_BORDER_H 46
#define ITEM_H       47
#define ITEM_W       210
#define MAX_FILES    24

#define FILE_NAME_MAX_LEN    96
#define FILE_PATH_MAX_LEN    300
#define DIR_NAME_FMT_MAX_LEN 64
#define SUBPATH_BUF_SIZE     512
#define SEND_PATH_BUF_SIZE   512
#define IR_FILE_MAX_BYTES    4096

#define TITLE_BAR_W        170
#define TITLE_BAR_H        30
#define TITLE_BAR_RADIUS   12
#define TITLE_BAR_BORDER_W 2

#define TOP_AREA_BORDER_W 3
#define OUTER_BORDER_W    3
#define ACCENT_BORDER_W   3

#define ITEM_RADIUS          10
#define ITEM_BORDER_SELECTED 3
#define ITEM_BORDER_NORMAL   1
#define ITEM_PAD_H           8
#define ITEM_PAD_COL         6
#define ITEM_FLEX_GROW       1

#define ITEMS_CONT_PAD      2
#define ITEMS_CONT_PAD_ROW  6
#define ITEMS_CONT_OFFSET_X 4
#define ITEMS_CONT_OFFSET_Y 4

#define SCROLL_BAR_TRACK_W        3
#define SCROLL_BAR_TRACK_OFF      10
#define SCROLL_BAR_IMG_OFF        4
#define SCROLL_BAR_THUMB_H        20
#define SCROLL_BAR_ANIM_MS        150
#define SCROLL_TRACK_X_FROM_RIGHT 10

#define NAV_TIMER_PERIOD_MS 50

#define SUBPATH_DIR_FMT TOS_PATH_IR "/%.64s"
#define FILE_NAME_FMT   "[%.30s] %.60s"
#define FILE_PATH_FMT   "%.128s/%.128s"
#define SEND_PATH_FMT   TOS_PATH_IR "/%.300s"

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_obj_t *s_items_cont = NULL;
static lv_obj_t *s_item_objs[MAX_FILES];
static lv_obj_t *s_scroll_bar = NULL;

static char s_file_names[MAX_FILES][FILE_NAME_MAX_LEN];
static char s_file_paths[MAX_FILES][FILE_PATH_MAX_LEN];
static size_t s_file_count = 0;
static size_t s_selected = 0;

static int32_t s_track_y_start;
static int32_t s_track_h;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void update_scroll_bar(void);
static void update_selection(void);
static void scan_ir_files(void);
static void send_selected(void);
static void build_list(void);
static void nav_timer_cb(lv_timer_t *t);

static void update_scroll_bar(void) {
  if (s_scroll_bar == NULL || s_file_count <= 1)
    return;

  int32_t pos = s_track_y_start + ((int32_t)s_selected * (s_track_h - SCROLL_BAR_THUMB_H)) /
                                      (int32_t)(s_file_count - 1);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_scroll_bar);
  lv_anim_set_values(&a, lv_obj_get_y(s_scroll_bar), pos);
  lv_anim_set_duration(&a, SCROLL_BAR_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&a);
}

static void update_selection(void) {
  for (size_t i = 0; i < s_file_count; i++) {
    if (i == s_selected) {
      lv_obj_set_style_border_width(s_item_objs[i], ITEM_BORDER_SELECTED, 0);
      lv_obj_set_style_border_color(s_item_objs[i], current_theme.border_accent, 0);
    } else {
      lv_obj_set_style_border_width(s_item_objs[i], ITEM_BORDER_NORMAL, 0);
      lv_obj_set_style_border_color(s_item_objs[i], current_theme.border_interface, 0);
    }
  }

  if (s_file_count > 0 && s_item_objs[s_selected] != NULL) {
    lv_obj_scroll_to_view(s_item_objs[s_selected], LV_ANIM_ON);
  }

  update_scroll_bar();
}

static void scan_ir_files(void) {
  s_file_count = 0;

  DIR *root = opendir(TOS_PATH_IR);
  if (root == NULL)
    return;

  struct dirent *proto_ent;
  while ((proto_ent = readdir(root)) != NULL && s_file_count < MAX_FILES) {
    if (proto_ent->d_name[0] == '.' || proto_ent->d_type != DT_DIR)
      continue;

    char sub_path[SUBPATH_BUF_SIZE];
    snprintf(sub_path, sizeof(sub_path), SUBPATH_DIR_FMT, proto_ent->d_name);

    DIR *sub = opendir(sub_path);
    if (sub == NULL)
      continue;

    struct dirent *file_ent;
    while ((file_ent = readdir(sub)) != NULL && s_file_count < MAX_FILES) {
      size_t len = strlen(file_ent->d_name);
      if (len < 4 || strcmp(file_ent->d_name + len - 3, ".ir") != 0)
        continue;

      snprintf(s_file_names[s_file_count],
               sizeof(s_file_names[0]),
               FILE_NAME_FMT,
               proto_ent->d_name,
               file_ent->d_name);

      snprintf(s_file_paths[s_file_count],
               sizeof(s_file_paths[0]),
               FILE_PATH_FMT,
               proto_ent->d_name,
               file_ent->d_name);

      s_file_count++;
    }
    closedir(sub);
  }
  closedir(root);
}

static void send_selected(void) {
  if (s_file_count == 0)
    return;

  char path[SEND_PATH_BUF_SIZE];
  snprintf(path, sizeof(path), SEND_PATH_FMT, s_file_paths[s_selected]);

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open IR file: %s", path);
    msgbox_open(LV_SYMBOL_WARNING, "Failed to open file", "OK", NULL, NULL);
    return;
  }

  fseek(f, 0, SEEK_END);
  int32_t sz = (int32_t)ftell(f);
  fseek(f, 0, SEEK_SET);

  if (sz <= 0 || sz > IR_FILE_MAX_BYTES) {
    ESP_LOGE(TAG, "Invalid IR file size: %ld", (long)sz);
    fclose(f);
    msgbox_open(LV_SYMBOL_WARNING, "Invalid file", "OK", NULL, NULL);
    return;
  }

  char *buf = malloc((size_t)sz + 1);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate IR file buffer");
    fclose(f);
    return;
  }

  size_t read = fread(buf, 1, (size_t)sz, f);
  fclose(f);

  if ((int32_t)read != sz) {
    ESP_LOGE(TAG, "Short read on IR file: expected %ld, got %zu", (long)sz, read);
    free(buf);
    msgbox_open(LV_SYMBOL_WARNING, "Failed to read file", "OK", NULL, NULL);
    return;
  }

  buf[sz] = '\0';

  ir_file_t ir_file;
  ir_file_init(&ir_file);

  if (ir_file_parse(buf, &ir_file) && ir_file.count > 0) {
    ir_tx_init();
    ir_file_send(&ir_file.signals[0]);
    msgbox_open(LV_SYMBOL_OK, "Signal sent!", "OK", NULL, NULL);
  } else {
    ESP_LOGW(TAG, "IR file parse failed or empty: %s", path);
    msgbox_open(LV_SYMBOL_WARNING, "Failed to send", "OK", NULL, NULL);
  }

  ir_file_free(&ir_file);
  free(buf);
}

static void build_list(void) {
  if (s_items_cont != NULL)
    lv_obj_clean(s_items_cont);

  scan_ir_files();

  for (size_t i = 0; i < s_file_count; i++) {
    lv_obj_t *item = lv_obj_create(s_items_cont);
    lv_obj_set_size(item, ITEM_W, ITEM_H);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(item, ITEM_RADIUS, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_color(item, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(item, ITEM_BORDER_NORMAL, 0);
    lv_obj_set_style_border_color(item, current_theme.border_interface, 0);
    lv_obj_set_style_pad_left(item, ITEM_PAD_H, 0);
    lv_obj_set_style_pad_right(item, ITEM_PAD_H, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, ITEM_PAD_COL, 0);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, s_file_names[i]);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(lbl, ITEM_FLEX_GROW);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *arrow = lv_label_create(item);
    lv_label_set_text(arrow, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(arrow, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_12, 0);

    s_item_objs[i] = item;
  }

  if (s_file_count == 0) {
    lv_obj_t *empty = lv_label_create(s_items_cont);
    lv_label_set_text(empty, "No .ir files found");
    lv_obj_set_style_text_color(empty, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
  }

  update_selection();
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;
  if (msgbox_is_open())
    return;

  bool up = up_button_is_down();
  bool down = down_button_is_down();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (down && !s_btn_down_last && s_file_count > 0) {
    s_selected = (s_selected + 1) % s_file_count;
    update_selection();
  }
  if (up && !s_btn_up_last && s_file_count > 0) {
    s_selected = (s_selected == 0) ? s_file_count - 1 : s_selected - 1;
    update_selection();
  }
  if (ok && !s_btn_ok_last) {
    send_selected();
  }
  if (back && !s_btn_back_last) {
    ui_switch_screen(SCREEN_IR_MENU);
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_ir_send_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_selected = 0;
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, OUTER_BORDER, 0);
  lv_obj_set_style_border_color(s_screen, current_theme.border_interface, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *top_area = lv_obj_create(s_screen);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, TOP_AREA_BORDER_W, 0);
  lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_BAR_W, TITLE_BAR_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, TITLE_BAR_RADIUS, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, TITLE_BAR_BORDER_W, 0);
  lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, "IR SEND");
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);

  int32_t items_y = TOP_BORDER_H + ITEMS_CONT_OFFSET_Y;
  int32_t items_h = LCD_V_RES - items_y - OUTER_BORDER - ITEMS_CONT_OFFSET_Y;

  s_items_cont = lv_obj_create(s_screen);
  lv_obj_set_size(s_items_cont, ITEM_W + ITEMS_CONT_PAD * 4, items_h);
  lv_obj_align(s_items_cont, LV_ALIGN_TOP_LEFT, ITEMS_CONT_OFFSET_X, items_y);
  lv_obj_set_style_bg_opa(s_items_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_items_cont, 0, 0);
  lv_obj_set_style_pad_all(s_items_cont, ITEMS_CONT_PAD, 0);
  lv_obj_set_style_pad_row(s_items_cont, ITEMS_CONT_PAD_ROW, 0);
  lv_obj_set_flex_flow(s_items_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_items_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_snap_y(s_items_cont, LV_SCROLL_SNAP_START);

  int32_t track_x = LCD_H_RES - OUTER_BORDER - SCROLL_TRACK_X_FROM_RIGHT;
  s_track_y_start = items_y + SCROLL_BAR_TRACK_OFF;
  s_track_h = items_h - SCROLL_BAR_TRACK_OFF * 2;

  // Points must outlive this function (used by lv_line)
  static lv_point_precise_t track_pts[2];
  track_pts[0].x = 0;
  track_pts[0].y = 0;
  track_pts[1].x = 0;
  track_pts[1].y = s_track_h;

  lv_obj_t *track = lv_line_create(s_screen);
  lv_line_set_points(track, track_pts, 2);
  lv_obj_set_pos(track, track_x, s_track_y_start);
  lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_width(track, SCROLL_BAR_TRACK_W, 0);
  lv_obj_set_style_line_dash_width(track, SCROLL_BAR_TRACK_W + 1, 0);
  lv_obj_set_style_line_dash_gap(track, SCROLL_BAR_TRACK_W + 1, 0);

  // Cached across calls: asset descriptor is constant after first load
  static lv_image_dsc_t *sb_dsc = NULL;
  if (sb_dsc == NULL)
    sb_dsc = assets_get("/assets/icons/slide_bar_v.bin");

  s_scroll_bar = lv_image_create(s_screen);
  if (sb_dsc != NULL)
    lv_image_set_src(s_scroll_bar, sb_dsc);

  lv_obj_set_pos(s_scroll_bar, track_x - SCROLL_BAR_IMG_OFF, s_track_y_start);
  lv_obj_move_foreground(s_scroll_bar);

  build_list();

  if (s_nav_timer == NULL) {
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_PERIOD_MS, NULL);
  }

  lv_screen_load(s_screen);
}
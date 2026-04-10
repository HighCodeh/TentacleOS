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

#include "files_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "st7789.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "storage_assets.h"
#include "text_viewer_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "FILES_UI";

#define NAV_TIMER_INTERVAL_MS   50
#define MAX_ENTRIES             20
#define ENTRY_NAME_MAX_LEN      64
#define PATH_MAX_LEN            256
#define FULL_PATH_MAX_LEN       384
#define OUTER_BORDER            4
#define TOP_BORDER_H            46
#define ITEM_H                  50
#define ITEM_W                  210
#define ITEM_BORDER_WIDTH       1
#define ITEM_SELECTED_WIDTH     2
#define ITEM_RADIUS             10
#define ITEM_PAD_H              8
#define ITEM_PAD_COL            6
#define ITEM_ICON_SCALE         128
#define TITLE_BAR_W             170
#define TITLE_BAR_H             30
#define TITLE_BAR_RADIUS        12
#define TITLE_BAR_BORDER_WIDTH  2
#define TITLE_ICON_SCALE        80
#define TOP_AREA_BORDER_WIDTH   3
#define PATH_LABEL_OFFSET_X     (-8)
#define PATH_LABEL_OFFSET_Y     4
#define PATH_LABEL_MARGIN_RIGHT 30
#define ITEMS_PATH_GAP          28
#define ITEMS_CONT_PAD          2
#define ITEMS_CONT_PAD_ROW      6
#define ITEMS_CONT_X_OFFSET     4
#define SCROLL_TRACK_OFFSET_X   10
#define SCROLL_TRACK_MARGIN     10
#define SCROLL_TRACK_WIDTH      3
#define SCROLL_TRACK_DASH_W     4
#define SCROLL_TRACK_DASH_GAP   4
#define SCROLL_BAR_OFFSET_X     (-4)
#define SCROLL_BAR_THUMB_H      20
#define SCROLL_ANIM_DURATION_MS 150
#define VIEWER_SCROLL_STEP      30
#define DEFAULT_PATH            "/assets"

#define COLOR_BORDER      current_theme.border_interface
#define COLOR_ITEM_BORDER current_theme.border_accent
#define COLOR_GRAD_LEFT   current_theme.border_interface
#define COLOR_GRAD_RIGHT  current_theme.bg_secondary
#define COLOR_SEL_BORDER  current_theme.border_accent

static lv_obj_t *s_screen_files = NULL;
static lv_timer_t *s_nav_timer = NULL;

static lv_obj_t *s_path_label = NULL;
static lv_obj_t *s_items_cont = NULL;
static lv_obj_t *s_item_objs[MAX_ENTRIES];
static lv_obj_t *s_scroll_bar = NULL;

static char s_current_path[PATH_MAX_LEN] = DEFAULT_PATH;
static char s_entry_names[MAX_ENTRIES][ENTRY_NAME_MAX_LEN];
static bool s_entry_is_dir[MAX_ENTRIES];
static int s_entry_count = 0;
static int s_selected = 0;
static bool s_is_viewing_file = false;
static text_viewer_t s_viewer;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_back_last = false;

static lv_font_t *s_file_font = NULL;

static int s_track_y_start;
static int s_track_h;

static void update_scroll_bar(void);
static void update_selection(void);
static void scan_directory(void);
static void build_file_list(void);
static void open_file_viewer(void);
static void close_file_viewer(void);
static void navigate_into(void);
static void navigate_back(void);
static void nav_timer_cb(lv_timer_t *timer);

void ui_files_open(void) {
  if (s_screen_files != NULL) {
    lv_obj_del(s_screen_files);
    s_screen_files = NULL;
  }

  s_screen_files = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_files, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen_files, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen_files, LV_OBJ_FLAG_SCROLLABLE);

  if (s_file_font == NULL) {
    extern lv_font_t *lv_binfont_create(const char *);
    s_file_font = lv_binfont_create("A:assets/fonts/Inter.bin");
  }

  lv_obj_set_style_border_width(s_screen_files, OUTER_BORDER, 0);
  lv_obj_set_style_border_color(s_screen_files, COLOR_BORDER, 0);
  lv_obj_set_style_radius(s_screen_files, 0, 0);
  lv_obj_set_style_pad_all(s_screen_files, 0, 0);

  lv_obj_t *top_area = lv_obj_create(s_screen_files);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, TOP_AREA_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(top_area, COLOR_BORDER, 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_BAR_W, TITLE_BAR_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, TITLE_BAR_RADIUS, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, COLOR_GRAD_LEFT, 0);
  lv_obj_set_style_bg_grad_color(title_bar, COLOR_GRAD_RIGHT, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, TITLE_BAR_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(title_bar, COLOR_ITEM_BORDER, 0);

  static lv_image_dsc_t *s_folder_icon = NULL;
  if (s_folder_icon == NULL)
    s_folder_icon = assets_get("/assets/frames/folder_frame_0.bin");

  if (s_folder_icon != NULL) {
    lv_obj_t *title_icon = lv_image_create(title_bar);
    lv_image_set_src(title_icon, s_folder_icon);
    lv_obj_add_flag(title_icon, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(title_icon, LV_ALIGN_LEFT_MID, ITEM_PAD_H, 0);
    lv_image_set_scale(title_icon, TITLE_ICON_SCALE);
  }

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, "Files");
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(
      title_lbl, s_file_font != NULL ? s_file_font : &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);

  int content_y = TOP_BORDER_H + 4;

  s_path_label = lv_label_create(s_screen_files);
  lv_label_set_text(s_path_label, s_current_path);
  lv_obj_set_style_text_color(s_path_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_path_label, &lv_font_montserrat_12, 0);
  lv_obj_set_width(s_path_label, LCD_H_RES - OUTER_BORDER * 2 - PATH_LABEL_MARGIN_RIGHT);
  lv_label_set_long_mode(s_path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(
      s_path_label, LV_ALIGN_TOP_MID, PATH_LABEL_OFFSET_X, content_y + PATH_LABEL_OFFSET_Y);

  int items_y = content_y + ITEMS_PATH_GAP;
  int items_h = LCD_V_RES - items_y - OUTER_BORDER - 4;

  s_items_cont = lv_obj_create(s_screen_files);
  lv_obj_set_size(s_items_cont, ITEM_W + 8, items_h);
  lv_obj_align(s_items_cont, LV_ALIGN_TOP_LEFT, ITEMS_CONT_X_OFFSET, items_y);
  lv_obj_set_style_bg_opa(s_items_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_items_cont, 0, 0);
  lv_obj_set_style_pad_all(s_items_cont, ITEMS_CONT_PAD, 0);
  lv_obj_set_style_pad_row(s_items_cont, ITEMS_CONT_PAD_ROW, 0);
  lv_obj_set_flex_flow(s_items_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_items_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_snap_y(s_items_cont, LV_SCROLL_SNAP_START);

  int track_x = LCD_H_RES - OUTER_BORDER - SCROLL_TRACK_OFFSET_X;
  s_track_y_start = items_y + SCROLL_TRACK_MARGIN;
  s_track_h = items_h - SCROLL_TRACK_MARGIN * 2;

  static lv_point_precise_t s_track_pts[2];
  s_track_pts[0].x = 0;
  s_track_pts[0].y = 0;
  s_track_pts[1].x = 0;
  s_track_pts[1].y = s_track_h;

  lv_obj_t *track = lv_line_create(s_screen_files);
  lv_line_set_points(track, s_track_pts, 2);
  lv_obj_set_pos(track, track_x, s_track_y_start);
  lv_obj_set_style_line_color(track, current_theme.text_main, 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_width(track, SCROLL_TRACK_WIDTH, 0);
  lv_obj_set_style_line_dash_width(track, SCROLL_TRACK_DASH_W, 0);
  lv_obj_set_style_line_dash_gap(track, SCROLL_TRACK_DASH_GAP, 0);

  static lv_image_dsc_t *s_sb_dsc = NULL;
  if (s_sb_dsc == NULL)
    s_sb_dsc = assets_get("/assets/icons/slide_bar_v.bin");

  s_scroll_bar = lv_image_create(s_screen_files);
  if (s_sb_dsc != NULL)
    lv_image_set_src(s_scroll_bar, s_sb_dsc);

  lv_obj_set_pos(s_scroll_bar, track_x + SCROLL_BAR_OFFSET_X, s_track_y_start);
  lv_obj_move_foreground(s_scroll_bar);

  strncpy(s_current_path, DEFAULT_PATH, sizeof(s_current_path) - 1);
  s_current_path[sizeof(s_current_path) - 1] = '\0';
  s_selected = 0;
  build_file_list();

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  lv_screen_load(s_screen_files);
}

static void update_scroll_bar(void) {
  if (s_scroll_bar == NULL || s_entry_count <= 1)
    return;

  int32_t pos =
      s_track_y_start + (s_selected * (s_track_h - SCROLL_BAR_THUMB_H)) / (s_entry_count - 1);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_scroll_bar);
  lv_anim_set_values(&a, lv_obj_get_y(s_scroll_bar), pos);
  lv_anim_set_duration(&a, SCROLL_ANIM_DURATION_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&a);
}

static void update_selection(void) {
  for (int i = 0; i < s_entry_count; i++) {
    if (i == s_selected) {
      lv_obj_set_style_border_width(s_item_objs[i], ITEM_SELECTED_WIDTH, 0);
      lv_obj_set_style_border_color(s_item_objs[i], COLOR_SEL_BORDER, 0);
    } else {
      lv_obj_set_style_border_width(s_item_objs[i], ITEM_BORDER_WIDTH, 0);
      lv_obj_set_style_border_color(s_item_objs[i], COLOR_ITEM_BORDER, 0);
    }
  }

  if (s_entry_count > 0 && s_item_objs[s_selected] != NULL)
    lv_obj_scroll_to_view(s_item_objs[s_selected], LV_ANIM_ON);

  update_scroll_bar();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void scan_directory(void) {
  s_entry_count = 0;

  DIR *dir = opendir(s_current_path);
  if (dir == NULL) {
    ESP_LOGE(TAG, "Failed to open: %s", s_current_path);
    return;
  }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL && s_entry_count < MAX_ENTRIES) {
    if (ent->d_name[0] == '.')
      continue;

    strncpy(s_entry_names[s_entry_count], ent->d_name, ENTRY_NAME_MAX_LEN - 1);
    s_entry_names[s_entry_count][ENTRY_NAME_MAX_LEN - 1] = '\0';

    char full[FULL_PATH_MAX_LEN];
    snprintf(full, sizeof(full), "%s/%s", s_current_path, ent->d_name);

    struct stat st;
    s_entry_is_dir[s_entry_count] = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
    s_entry_count++;
  }

  closedir(dir);
}
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void open_file_viewer(void) {
  char full_path[FULL_PATH_MAX_LEN];
  snprintf(full_path, sizeof(full_path), "%s/%s", s_current_path, s_entry_names[s_selected]);

  s_viewer = text_viewer_create(s_screen_files, s_entry_names[s_selected]);
  text_viewer_load_file(&s_viewer, full_path);
  lv_obj_move_foreground(s_viewer.screen);
  s_is_viewing_file = true;
}
#pragma GCC diagnostic pop

static void close_file_viewer(void) {
  if (s_viewer.screen != NULL) {
    lv_obj_del(s_viewer.screen);
    s_viewer.screen = NULL;
  }
  s_is_viewing_file = false;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void navigate_into(void) {
  if (s_entry_count == 0)
    return;

  if (!s_entry_is_dir[s_selected]) {
    open_file_viewer();
    return;
  }

  char new_path[PATH_MAX_LEN];
  snprintf(new_path, sizeof(new_path), "%s/%s", s_current_path, s_entry_names[s_selected]);
  strncpy(s_current_path, new_path, sizeof(s_current_path) - 1);
  s_current_path[sizeof(s_current_path) - 1] = '\0';
  s_selected = 0;
  build_file_list();
}
#pragma GCC diagnostic pop

static void navigate_back(void) {
  char *last = strrchr(s_current_path, '/');
  if (last == NULL || last == s_current_path)
    return;

  *last = '\0';
  s_selected = 0;
  build_file_list();
}

static void build_file_list(void) {
  if (s_items_cont != NULL)
    lv_obj_clean(s_items_cont);

  scan_directory();

  if (s_path_label != NULL)
    lv_label_set_text(s_path_label, s_current_path);

  static lv_image_dsc_t *s_folder_dsc = NULL;
  static lv_image_dsc_t *s_file_dsc = NULL;

  if (s_folder_dsc == NULL)
    s_folder_dsc = assets_get("/assets/frames/folder_frame_0.bin");
  if (s_file_dsc == NULL)
    s_file_dsc = assets_get("/assets/frames/file_frame_0.bin");

  for (int i = 0; i < s_entry_count; i++) {
    lv_obj_t *item = lv_obj_create(s_items_cont);
    lv_obj_set_size(item, ITEM_W, ITEM_H);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(item, ITEM_RADIUS, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, COLOR_GRAD_LEFT, 0);
    lv_obj_set_style_bg_grad_color(item, COLOR_GRAD_RIGHT, 0);
    lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(item, ITEM_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(item, COLOR_ITEM_BORDER, 0);
    lv_obj_set_style_pad_left(item, ITEM_PAD_H, 0);
    lv_obj_set_style_pad_right(item, ITEM_PAD_H, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, ITEM_PAD_COL, 0);

    lv_image_dsc_t *dsc = s_entry_is_dir[i] ? s_folder_dsc : s_file_dsc;
    if (dsc != NULL) {
      lv_obj_t *icon = lv_image_create(item);
      lv_image_set_src(icon, dsc);
      lv_image_set_scale(icon, ITEM_ICON_SCALE);
    }

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, s_entry_names[i]);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    if (s_entry_is_dir[i]) {
      lv_obj_t *arrow = lv_label_create(item);
      lv_label_set_text(arrow, LV_SYMBOL_REFRESH);
      lv_obj_set_style_text_color(arrow, current_theme.border_accent, 0);
      lv_obj_set_style_text_font(arrow, &lv_font_montserrat_12, 0);
    }

    s_item_objs[i] = item;
  }

  if (s_entry_count == 0) {
    lv_obj_t *empty = lv_label_create(s_items_cont);
    lv_label_set_text(empty, "Empty folder");
    lv_obj_set_style_text_color(empty, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
  }

  update_selection();
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen_files) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }

  if (ui_input_is_locked())
    return;

  bool is_up = up_button_is_down();
  bool is_down = down_button_is_down();
  bool is_left = left_button_is_down();
  bool is_right = right_button_is_down();
  bool is_back = back_button_is_down();

  if (s_is_viewing_file) {
    if (is_down && !s_btn_down_last && s_viewer.text_area != NULL)
      lv_obj_scroll_by(s_viewer.text_area, 0, -VIEWER_SCROLL_STEP, LV_ANIM_ON);

    if (is_up && !s_btn_up_last && s_viewer.text_area != NULL)
      lv_obj_scroll_by(s_viewer.text_area, 0, VIEWER_SCROLL_STEP, LV_ANIM_ON);

    if ((is_left && !s_btn_left_last) || (is_back && !s_btn_back_last))
      close_file_viewer();

    s_btn_up_last = is_up;
    s_btn_down_last = is_down;
    s_btn_left_last = is_left;
    s_btn_right_last = is_right;
    s_btn_back_last = is_back;
    return;
  }

  if (is_down && !s_btn_down_last && s_entry_count > 0) {
    s_selected = (s_selected + 1) % s_entry_count;
    update_selection();
  }

  if (is_up && !s_btn_up_last && s_entry_count > 0) {
    s_selected = (s_selected == 0) ? s_entry_count - 1 : s_selected - 1;
    update_selection();
  }

  if (is_right && !s_btn_right_last)
    navigate_into();

  if (is_left && !s_btn_left_last)
    navigate_back();

  if (is_back && !s_btn_back_last) {
    ui_switch_screen(SCREEN_MENU);
    return;
  }

  s_btn_up_last = is_up;
  s_btn_down_last = is_down;
  s_btn_left_last = is_left;
  s_btn_right_last = is_right;
  s_btn_back_last = is_back;
}
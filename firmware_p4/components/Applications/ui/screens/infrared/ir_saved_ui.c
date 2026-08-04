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

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "capture_result_ui.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_SAVED_UI";

#define NAV_TIMER_MS 50
#define NAME_LEN     24

#define IR_ICON        "/assets/icons/settings_input_antenna.bin"
#define IR_SIGNAL_ICON "/assets/icons/settings_remote.bin"
#define IR_CARRIER     "38 kHz"

#define FILES_COL_DIM   0x8A8594
#define IRC_LEFT        6
#define IRC_GUTTER      16
#define IRC_TOP_Y       46
#define IRC_LIST_PAD    2
#define IRC_LIST_ROW    8
#define IRC_CARD_H      86
#define IRC_CARD_RADIUS 12
#define IRC_CARD_PAD    10
#define IRC_GLOW_W      14
#define IRC_TRACK_X     227
#define IRC_TRACK_Y     54
#define IRC_TRACK_LEN   232
#define IRC_THUMB_H     45
#define IRC_THUMB_ICON  "/assets/icons/drag_indicator.bin"

static const char *MOCK_PROTOCOLS[] = {"NEC", "SAMSUNG", "RC5", "SONY"};
#define MOCK_PROTOCOLS_COUNT ((int)(sizeof(MOCK_PROTOCOLS) / sizeof(MOCK_PROTOCOLS[0])))

static const char *MOCK_FILES[] = {
    "power", "vol_up", "vol_down", "mute", "source", "ch_up", "ch_down", "menu", "ok", "back"};
#define MOCK_FILES_COUNT ((int)(sizeof(MOCK_FILES) / sizeof(MOCK_FILES[0])))

typedef enum {
  LEVEL_PROTOCOLS = 0,
  LEVEL_FILES,
  LEVEL_ACTIONS,
} browse_level_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static capture_result_t s_cr = {0};
static lv_timer_t *s_nav_timer = NULL;

static browse_level_t s_level = LEVEL_PROTOCOLS;
static int s_proto = 0;
static int s_file = 0;
static bool s_saved = false;

static char s_names[MOCK_FILES_COUNT][NAME_LEN];
static int s_file_count = 0;

static lv_obj_t *s_file_list = NULL;
static lv_obj_t *s_file_rows[MOCK_FILES_COUNT];
static lv_obj_t *s_file_names[MOCK_FILES_COUNT];
static lv_obj_t *s_file_values[MOCK_FILES_COUNT];
static lv_obj_t *s_file_thumb = NULL;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void build_screen(void);
static void nav_timer_cb(lv_timer_t *t);

static void reset_files(void) {
  s_file_count = MOCK_FILES_COUNT;
  for (int i = 0; i < s_file_count; i++)
    snprintf(s_names[i], NAME_LEN, "%s", MOCK_FILES[i]);
  s_file = 0;
}

static void rebuild_async(void *p) {
  (void)p;
  build_screen();
}

static void on_rename_submit(const char *text, void *ud) {
  (void)ud;
  if (text == NULL || text[0] == '\0')
    return;
  snprintf(s_names[s_file], NAME_LEN, "%s", text);
  ui_feedback(UI_FB_WRITE);
  notify(NOTIFY_INFO, "Signal renamed");
  lv_async_call(rebuild_async, NULL);
}

static void on_delete_confirm(bool confirm) {
  if (!confirm)
    return;
  for (int i = s_file; i < s_file_count - 1; i++)
    memmove(s_names[i], s_names[i + 1], NAME_LEN);
  s_file_count--;
  ui_feedback(UI_FB_WRITE);
  notify(NOTIFY_INFO, "Signal deleted");
  s_level = (s_file_count > 0) ? LEVEL_FILES : LEVEL_PROTOCOLS;
  lv_async_call(rebuild_async, NULL);
}

static void move_file_thumb(void) {
  if (s_file_thumb == NULL || s_file_count <= 1)
    return;
  int thumb_h = lv_obj_get_height(s_file_thumb);
  if (thumb_h <= 0)
    thumb_h = IRC_THUMB_H;
  int travel = IRC_TRACK_LEN - thumb_h;
  if (travel < 0)
    travel = 0;
  int pos = IRC_TRACK_Y + (s_file * travel) / (s_file_count - 1);
  lv_obj_set_y(s_file_thumb, pos);
}

static void style_file_row(int i, bool sel) {
  lv_obj_t *card = s_file_rows[i];
  if (card == NULL)
    return;
  if (sel) {
    lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(card, IRC_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(card, -2, 0);
    lv_obj_set_style_text_color(s_file_values[i], current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_file_values[i], lv_color_hex(FILES_COL_DIM), 0);
  }
}

static void update_file_selection(void) {
  for (int i = 0; i < s_file_count; i++)
    style_file_row(i, i == s_file);
  if (s_file_list != NULL && s_file >= 0 && s_file < s_file_count && s_file_rows[s_file] != NULL) {
    lv_obj_update_layout(s_file_list);
    lv_obj_scroll_to_view(s_file_rows[s_file], LV_ANIM_ON);
  }
  move_file_thumb();
}

static void build_files_list(void) {
  if (s_file >= s_file_count)
    s_file = s_file_count - 1;
  if (s_file < 0)
    s_file = 0;

  ui_chrome_header(s_screen, MOCK_PROTOCOLS[s_proto], IR_ICON);

  lv_obj_t *cont = lv_obj_create(s_screen);
  s_file_list = cont;
  lv_obj_set_size(
      cont, LCD_H_RES - IRC_LEFT - IRC_GUTTER, LCD_V_RES - IRC_TOP_Y - UI_CHROME_FOOTER_H - 4);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, IRC_LEFT, IRC_TOP_Y);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, IRC_LIST_PAD, 0);
  lv_obj_set_style_pad_row(cont, IRC_LIST_ROW, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);

  static const lv_point_precise_t ir_pulse_pts[] = {
      {0, 20},  {0, 4},    {10, 4},   {10, 20}, {24, 20}, {24, 4},   {28, 4},  {28, 20},  {46, 20},
      {46, 4},  {50, 4},   {50, 20},  {78, 20}, {78, 4},  {82, 4},   {82, 20}, {112, 20}, {112, 4},
      {116, 4}, {116, 20}, {150, 20}, {150, 4}, {154, 4}, {154, 20}, {190, 20}};

  for (int i = 0; i < s_file_count; i++) {
    lv_obj_t *card = lv_obj_create(cont);
    s_file_rows[i] = card;
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(card, lv_pct(100), IRC_CARD_H);
    lv_obj_set_style_radius(card, IRC_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(card, IRC_CARD_PAD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_color(card, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);

    lv_obj_t *name = lv_label_create(card);
    s_file_names[i] = name;
    lv_obj_set_width(name, lv_pct(66));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, s_names[i]);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, current_theme.text_main, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *value = lv_label_create(card);
    s_file_values[i] = value;
    lv_label_set_text(value, IR_CARRIER);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(value, current_theme.border_accent, 0);
    lv_obj_align(value, LV_ALIGN_TOP_RIGHT, 0, 2);

    lv_obj_t *proto = lv_label_create(card);
    lv_obj_set_width(proto, lv_pct(100));
    lv_label_set_long_mode(proto, LV_LABEL_LONG_DOT);
    lv_label_set_text(proto, MOCK_PROTOCOLS[s_proto]);
    lv_obj_set_style_text_font(proto, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(proto, lv_color_hex(FILES_COL_DIM), 0);
    lv_obj_align(proto, LV_ALIGN_TOP_LEFT, 0, 20);

    lv_obj_t *pulse = lv_line_create(card);
    lv_line_set_points(pulse, ir_pulse_pts, sizeof(ir_pulse_pts) / sizeof(ir_pulse_pts[0]));
    lv_obj_align(pulse, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_line_color(pulse, current_theme.border_accent, 0);
    lv_obj_set_style_line_opa(pulse, LV_OPA_COVER, 0);
    lv_obj_set_style_line_width(pulse, 2, 0);
  }

  static lv_point_precise_t ir_track_pts[2];
  ir_track_pts[0].x = 0;
  ir_track_pts[0].y = 0;
  ir_track_pts[1].x = 0;
  ir_track_pts[1].y = IRC_TRACK_LEN;
  lv_obj_t *track = lv_line_create(s_screen);
  lv_line_set_points(track, ir_track_pts, 2);
  lv_obj_set_pos(track, IRC_TRACK_X, IRC_TRACK_Y);
  lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_width(track, 3, 0);
  lv_obj_set_style_line_dash_width(track, 4, 0);
  lv_obj_set_style_line_dash_gap(track, 4, 0);

  lv_image_dsc_t *thumb = assets_get(IRC_THUMB_ICON);
  s_file_thumb = lv_image_create(s_screen);
  if (thumb != NULL)
    lv_image_set_src(s_file_thumb, thumb);
  lv_obj_set_pos(s_file_thumb, IRC_TRACK_X - 4, IRC_TRACK_Y);
  lv_obj_move_foreground(s_file_thumb);

  lv_obj_update_layout(cont);
  update_file_selection();

  ui_chrome_footer(s_screen, "OK Open   BACK Back");
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_file_list = NULL;
  s_file_thumb = NULL;
  s_cr = (capture_result_t){0};

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  if (s_level == LEVEL_PROTOCOLS) {
    s_menu = menu_component_create(s_screen, "BROWSE SIGNALS", "/assets/icons/folder_open.bin");
    for (int i = 0; i < MOCK_PROTOCOLS_COUNT; i++)
      menu_component_add_item(&s_menu, "/assets/icons/folder.bin", MOCK_PROTOCOLS[i]);
    menu_component_set_hint(&s_menu, "OK Open   BACK Exit");
  } else if (s_level == LEVEL_FILES) {
    build_files_list();
  } else {
    ui_chrome_header(s_screen, s_names[s_file], IR_ICON);
    capture_result_cfg_t cfg = {
        .accent = current_theme.border_accent,
        .card_icon = IR_ICON,
        .card_title = s_names[s_file],
        .card_sub = MOCK_PROTOCOLS[s_proto],
        .card_value = "cmd 0x04 / 0x08",
        .primary_label = "Send",
        .again_label = "Rename",
    };
    s_cr = capture_result_create(s_screen, &cfg);
    if (s_saved)
      capture_result_mark_saved(&s_cr);
    ui_chrome_footer(s_screen, "UP/DOWN choose   OK do   BACK back");
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

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (msgbox_is_open() || keyboard_is_open() || ui_input_is_locked()) {
    s_btn_up_last = up;
    s_btn_down_last = down;
    s_btn_left_last = left;
    s_btn_ok_last = ok;
    s_btn_back_last = back;
    return;
  }

  bool up_e = up && !s_btn_up_last;
  bool down_e = down && !s_btn_down_last;
  bool left_e = left && !s_btn_left_last;
  bool ok_e = ok && !s_btn_ok_last;
  bool back_e = back && !s_btn_back_last;

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_ok_last = ok;
  s_btn_back_last = back;

  if (s_level == LEVEL_ACTIONS) {
    if (down_e) {
      capture_result_next(&s_cr);
      ui_feedback(UI_FB_NAV);
    }
    if (up_e) {
      capture_result_prev(&s_cr);
      ui_feedback(UI_FB_NAV);
    }
    if (ok_e) {
      switch (capture_result_selected(&s_cr)) {
        case CAP_ACT_PRIMARY:
          ui_feedback(UI_FB_SELECT);
          ESP_LOGI(TAG, "mock send: %s/%s", MOCK_PROTOCOLS[s_proto], s_names[s_file]);
          ui_switch_screen(SCREEN_IR_SEND);
          return;
        case CAP_ACT_SAVE:
          if (!s_saved) {
            s_saved = true;
            capture_result_mark_saved(&s_cr);
            ui_feedback(UI_FB_WRITE);
            notify(NOTIFY_SAVED, "Signal saved");
          }
          break;
        case CAP_ACT_AGAIN:
          keyboard_open(NULL, on_rename_submit, NULL);
          break;
        case CAP_ACT_DISCARD:
          msgbox_open(
              LV_SYMBOL_TRASH, "Delete this signal?", "Delete", "Cancel", on_delete_confirm);
          break;
        default:
          break;
      }
    }
    if (back_e || left_e) {
      s_level = LEVEL_FILES;
      build_screen();
      return;
    }
    return;
  }

  if (s_level == LEVEL_PROTOCOLS) {
    if (down_e) {
      menu_component_next(&s_menu);
      ui_feedback(UI_FB_NAV);
    }
    if (up_e) {
      menu_component_prev(&s_menu);
      ui_feedback(UI_FB_NAV);
    }
    if (ok_e) {
      s_proto = menu_component_get_selected(&s_menu);
      reset_files();
      s_level = LEVEL_FILES;
      ui_feedback(UI_FB_SELECT);
      build_screen();
      return;
    }
    if (back_e || left_e)
      ui_switch_screen(SCREEN_IR_MENU);
    return;
  }

  if (down_e && s_file < s_file_count - 1) {
    s_file++;
    update_file_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (up_e && s_file > 0) {
    s_file--;
    update_file_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (ok_e) {
    s_saved = false;
    s_level = LEVEL_ACTIONS;
    ui_feedback(UI_FB_SELECT);
    build_screen();
    return;
  }
  if (back_e || left_e) {
    s_level = LEVEL_PROTOCOLS;
    build_screen();
    return;
  }
}

void ui_ir_saved_open(void) {
  s_level = LEVEL_PROTOCOLS;
  s_proto = 0;
  s_file = 0;
  s_saved = false;
  reset_files();
  s_btn_up_last = false;
  s_btn_down_last = false;
  s_btn_left_last = false;
  s_btn_ok_last = false;
  s_btn_back_last = false;
  build_screen();
}

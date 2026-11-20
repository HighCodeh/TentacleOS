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

#include "esp_attr.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "capture_result_ui.h"
#include "ir_store.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static const char *TAG = "IR_SAVED_UI";

#define MAX_FILES       IR_STORE_MAX_ENTRIES
#define SEND_STATUS_LEN (IR_STORE_NAME_MAX + 24)

#define IR_ICON        "/assets/icons/settings_input_antenna.bin"
#define IR_SIGNAL_ICON "/assets/icons/settings_remote.bin"

#define IRC_LEFT        6
#define IRC_GUTTER      16
#define IRC_TOP_Y       46
#define IRC_LIST_PAD    2
#define IRC_LIST_ROW    8
#define IRC_CARD_H      86
#define IRC_CARD_RADIUS 12
#define IRC_CARD_PAD    10
#define IRC_GLOW_W      14
#define IRC_TRACK_X     (ui_screen_w() - 13)
#define IRC_TRACK_Y     54
#define IRC_TRACK_LEN   LV_MIN(232, ui_screen_h() - UI_CHROME_FOOTER_H - IRC_TRACK_Y)
#define IRC_THUMB_H     45
#define IRC_THUMB_ICON  "/assets/icons/drag_indicator.bin"

typedef enum {
  LEVEL_FILES = 0,
  LEVEL_ACTIONS,
} browse_level_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static capture_result_t s_cr = {0};

static browse_level_t s_level = LEVEL_FILES;
static int s_file = 0;

EXT_RAM_BSS_ATTR static ir_store_entry_t s_files[MAX_FILES];
static int s_file_count = 0;

static lv_obj_t *s_file_list = NULL;
static lv_obj_t *s_file_rows[MAX_FILES];
static lv_obj_t *s_file_names[MAX_FILES];
static lv_obj_t *s_file_thumb = NULL;

static void build_screen(void);
static void ir_saved_input(const input_event_t *ev, void *ctx);

static void reload_all(void) {
  s_file_count = ir_store_list(s_files, MAX_FILES);
  if (s_file_count < 0)
    s_file_count = 0;
  if (s_file >= s_file_count)
    s_file = s_file_count > 0 ? s_file_count - 1 : 0;
}

static void selected_value(char *buf, size_t cap) {
  buf[0] = '\0';
  ir_file_t f;
  ir_file_init(&f);
  if (ir_store_load(s_files[s_file].path, &f) == ESP_OK && f.count > 0) {
    ir_signal_t *s = &f.signals[0];
    if (!s->is_raw && s->data.protocol != IR_PROTO_UNKNOWN)
      snprintf(buf,
               cap,
               "cmd 0x%02lX / 0x%02lX",
               (unsigned long)s->data.address,
               (unsigned long)s->data.command);
    else
      snprintf(buf, cap, "raw signal");
  } else {
    snprintf(buf, cap, "--");
  }
  ir_file_free(&f);
}

static void send_selected(void) {
  ir_file_t file;
  ir_file_init(&file);
  esp_err_t err = ir_store_load(s_files[s_file].path, &file);
  if (err == ESP_OK)
    err = file.count > 0 ? ir_store_send_signal(&file.signals[0]) : ESP_ERR_NOT_FOUND;
  ir_file_free(&file);
  char message[SEND_STATUS_LEN];
  if (err == ESP_OK) {
    snprintf(message, sizeof(message), "Sent: %s", s_files[s_file].name);
    notify(NOTIFY_INFO, message);
  } else {
    ESP_LOGW(TAG, "Send failed for %s: %s", s_files[s_file].name, esp_err_to_name(err));
    notify(NOTIFY_WARNING, "Send failed; check signal file");
  }
}

static void rebuild_async(void *p) {
  (void)p;
  build_screen();
}

static void on_rename_submit(const char *text, void *ud) {
  (void)ud;
  if (text == NULL || text[0] == '\0')
    return;
  if (strcmp(s_files[s_file].name, text) != 0 && ir_store_exists(text)) {
    notify(NOTIFY_WARNING, "Name exists; choose another");
    keyboard_open_filename("Rename IR signal", text, IR_STORE_NAME_MAX - 1, on_rename_submit);
    return;
  }
  if (strcmp(s_files[s_file].name, text) == 0 ||
      ir_store_rename(s_files[s_file].name, text) == ESP_OK) {
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_INFO, "Signal renamed");
  } else {
    notify(NOTIFY_WARNING, "Rename failed");
  }
  reload_all();
  for (int i = 0; i < s_file_count; i++) {
    if (strcmp(s_files[i].name, text) == 0) {
      s_file = i;
      break;
    }
  }
  if (s_file_count == 0)
    s_level = LEVEL_FILES;
  ui_async_call(rebuild_async, NULL);
}

static void on_delete_confirm(bool confirm) {
  if (!confirm)
    return;
  if (ir_store_delete(s_files[s_file].name) != ESP_OK) {
    notify(NOTIFY_WARNING, "Delete failed; signal retained");
    return;
  }
  ui_feedback(UI_FB_WRITE);
  notify(NOTIFY_INFO, "Signal deleted");
  reload_all();
  s_level = LEVEL_FILES;
  ui_async_call(rebuild_async, NULL);
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
  } else {
    lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
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

  ui_chrome_header(s_screen, "SAVED SIGNALS", IR_ICON);

  lv_obj_t *cont = lv_obj_create(s_screen);
  s_file_list = cont;
  lv_obj_set_size(cont,
                  ui_screen_w() - IRC_LEFT - IRC_GUTTER,
                  ui_screen_h() - IRC_TOP_Y - UI_CHROME_FOOTER_H - 4);
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
    lv_obj_set_width(name, lv_pct(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, s_files[i].name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, current_theme.text_main, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *proto = lv_label_create(card);
    lv_obj_set_width(proto, lv_pct(100));
    lv_label_set_long_mode(proto, LV_LABEL_LONG_DOT);
    lv_label_set_text(proto, s_files[i].proto);
    lv_obj_set_style_text_font(proto, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(proto, current_theme.text_secondary, 0);
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

  s_screen = ui_chrome_screen_create();

  if (s_file_count == 0) {
    s_level = LEVEL_FILES;
    s_menu = menu_component_create(s_screen, "SAVED SIGNALS", IR_ICON);
    menu_component_add_item(&s_menu, IR_SIGNAL_ICON, "No saved signals");
    menu_component_set_hint(&s_menu, "Learn a signal first   BACK Exit");
  } else if (s_level == LEVEL_FILES) {
    build_files_list();
  } else {
    char value[32];
    selected_value(value, sizeof(value));
    ui_chrome_header(s_screen, s_files[s_file].name, IR_ICON);
    capture_result_cfg_t cfg = {
        .accent = current_theme.border_accent,
        .card_icon = IR_ICON,
        .card_title = s_files[s_file].name,
        .card_sub = s_files[s_file].proto,
        .card_value = value,
        .primary_label = "Send",
        .again_label = "Rename",
    };
    const capture_result_action_cfg_t actions[] = {
        {.id = CAP_ACT_PRIMARY, .symbol = LV_SYMBOL_UPLOAD, .label = "Send"},
        {.id = CAP_ACT_AGAIN, .symbol = LV_SYMBOL_EDIT, .label = "Rename"},
        {.id = CAP_ACT_DISCARD, .symbol = LV_SYMBOL_TRASH, .label = "Delete"},
    };
    s_cr =
        capture_result_create_custom(s_screen, &cfg, actions, sizeof(actions) / sizeof(actions[0]));
    ui_chrome_footer(s_screen, "UP/DOWN choose   OK do   BACK back");
  }

  ui_input_set_screen_handler(ir_saved_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void ir_saved_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_level == LEVEL_ACTIONS) {
    switch (ev->button) {
      case INPUT_BTN_DOWN:
        if (nav) {
          capture_result_next(&s_cr);
          ui_feedback(UI_FB_NAV);
        }
        break;
      case INPUT_BTN_UP:
        if (nav) {
          capture_result_prev(&s_cr);
          ui_feedback(UI_FB_NAV);
        }
        break;
      case INPUT_BTN_OK:
        if (press) {
          switch (capture_result_selected(&s_cr)) {
            case CAP_ACT_PRIMARY:
              ui_feedback(UI_FB_EMULATE);
              send_selected();
              break;
            case CAP_ACT_AGAIN:
              keyboard_open_filename("Rename IR signal",
                                     s_files[s_file].name,
                                     IR_STORE_NAME_MAX - 1,
                                     on_rename_submit);
              break;
            case CAP_ACT_DISCARD:
              msgbox_open(
                  LV_SYMBOL_TRASH, "Delete this signal?", "Delete", "Cancel", on_delete_confirm);
              break;
            default:
              break;
          }
        }
        break;
      case INPUT_BTN_BACK:
        if (press) {
          s_level = LEVEL_FILES;
          build_screen();
        }
        break;
      default:
        break;
    }
    return;
  }

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav && s_file < s_file_count - 1) {
        s_file++;
        update_file_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && s_file > 0) {
        s_file--;
        update_file_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press && s_file_count > 0) {
        s_level = LEVEL_ACTIONS;
        ui_feedback(UI_FB_SELECT);
        build_screen();
      }
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_IR_MENU);
      break;
    default:
      break;
  }
}

void ui_ir_saved_open(void) {
  s_level = LEVEL_FILES;
  s_file = 0;
  reload_all();
  build_screen();
}

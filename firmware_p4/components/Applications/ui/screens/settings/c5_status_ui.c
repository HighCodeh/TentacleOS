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

#include "c5_status_ui.h"

#include <stdint.h>
#include <string.h>

#include "st7789.h"

#include "notify_ui.h"
#include "ota_version.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define MX           8
#define CONTENT_W    (ui_screen_w() - 2 * MX)
#define INFO_Y       50
#define INFO_H       98
#define INFO_ROW_GAP 26
#define ACT_Y        158
#define ROW_H        36
#define ROW_GAP      6
#define ROW_STEP     (ROW_H + ROW_GAP)

#define COL_RAISE 0x170A28

#define HDR_ICON  "/assets/icons/developer_board.bin"
#define HDR_TITLE "C5 STATUS"

#define C5_VER_TIMEOUT_MS 500
#define C5_VER_BUF_LEN    24

enum {
  ACT_DOWNLOAD = 0,
  ACT_PING,
  ACT_INFO,
  ACT_COUNT,
};

typedef struct {
  const char *sym;
  const char *name;
  notify_type_t toast_type;
  const char *toast_text;
} action_def_t;

static const action_def_t ACTIONS[ACT_COUNT] = {
    {LV_SYMBOL_DOWNLOAD, "Enter Download", NOTIFY_INFO, "C5 entering download mode"},
    {LV_SYMBOL_REFRESH, "Legacy Ping", NOTIFY_INFO, "C5 ping: pong in 12 ms"},
    {LV_SYMBOL_LIST, "Get Info", NOTIFY_SAVED, "C5 v0.9.1  heap 41 KB free"},
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_act_row[ACT_COUNT];
static lv_obj_t *s_act_icon[ACT_COUNT];
static lv_obj_t *s_act_name[ACT_COUNT];

static int s_sel = 0;

static lv_obj_t *info_row(lv_obj_t *card, int index, const char *tag_txt, const char *val_txt) {
  lv_obj_t *tag = lv_label_create(card);
  lv_label_set_text(tag, tag_txt);
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(tag, current_theme.text_main, 0);
  lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 0, index * INFO_ROW_GAP);

  lv_obj_t *val = lv_label_create(card);
  lv_label_set_text(val, val_txt);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(val, current_theme.text_secondary, 0);
  lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, index * INFO_ROW_GAP);
  return val;
}

static void build_info_card(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, INFO_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, INFO_Y);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_hor(card, 14, 0);
  lv_obj_set_style_pad_ver(card, 8, 0);
  lv_obj_set_style_shadow_width(card, 16, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);

  bool alive = spi_bridge_is_alive();
  char cver[C5_VER_BUF_LEN] = "?";
  if (alive) {
    spi_header_t hdr;
    char ver[C5_VER_BUF_LEN] = {0};
    if (spi_bridge_send_command(
            SPI_ID_SYSTEM_VERSION, NULL, 0, &hdr, (uint8_t *)ver, sizeof(ver), C5_VER_TIMEOUT_MS) ==
            ESP_OK &&
        ver[0] != '\0')
      strlcpy(cver, ver, sizeof(cver));
  }

  lv_obj_t *link_val = info_row(card, 0, "Link", alive ? "ALIVE" : "DOWN");
  if (alive)
    lv_obj_set_style_text_color(link_val, lv_color_hex(UI_COL_SUCCESS), 0);
  info_row(card, 1, "C5 FW", cver);
  info_row(card, 2, "Expected", FIRMWARE_VERSION);
}

static lv_obj_t *make_action_row(lv_obj_t *parent, int i) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, CONTENT_W, ROW_H);
  lv_obj_align(
      row,
      LV_ALIGN_TOP_MID,
      0,
      LV_MIN(ACT_Y, ui_screen_h() - UI_CHROME_FOOTER_H - ((ACT_COUNT - 1) * ROW_STEP + ROW_H)) +
          i * ROW_STEP);
  lv_obj_set_style_radius(row, 9, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_left(row, 12, 0);
  lv_obj_set_style_pad_right(row, 12, 0);
  lv_obj_set_style_pad_top(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ic = lv_label_create(row);
  lv_label_set_text(ic, ACTIONS[i].sym);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_16, 0);
  lv_obj_set_width(ic, 22);
  lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, ACTIONS[i].name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_left(name, 10, 0);
  lv_obj_set_flex_grow(name, 1);

  s_act_icon[i] = ic;
  s_act_name[i] = name;
  return row;
}

static void refresh_selection(void) {
  const lv_color_t accent = current_theme.border_accent;
  const lv_color_t dim = current_theme.text_secondary;
  for (int i = 0; i < ACT_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(s_act_row[i], sel ? accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(s_act_row[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        s_act_row[i], sel ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
    lv_obj_set_style_shadow_width(s_act_row[i], sel ? 14 : 0, 0);
    lv_obj_set_style_shadow_color(s_act_row[i], accent, 0);
    lv_obj_set_style_shadow_spread(s_act_row[i], sel ? -3 : 0, 0);
    lv_obj_set_style_text_color(s_act_icon[i], sel ? accent : dim, 0);
    lv_obj_set_style_text_color(s_act_name[i], sel ? current_theme.text_main : dim, 0);
  }
}

static void c5_status_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS_DEV);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % ACT_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel - 1 + ACT_COUNT) % ACT_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        notify(ACTIONS[s_sel].toast_type, ACTIONS[s_sel].toast_text);
        ui_feedback(UI_FB_SELECT);
      }
      break;
    default:
      break;
  }
}

void ui_c5_status_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_info_card();
  for (int i = 0; i < ACT_COUNT; i++)
    s_act_row[i] = make_action_row(s_screen, i);
  refresh_selection();

  ui_chrome_footer(s_screen, "UP/DOWN select   OK run   BACK exit");

  ui_input_set_screen_handler(c5_status_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

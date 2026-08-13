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

#include "ble_radio_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 60

#define RADIO_ICON "/assets/icons/bluetooth.bin"

#define MX        10
#define CONTENT_W (LCD_H_RES - 2 * MX)
#define CARD_Y    50
#define CARD_H    58
#define ROWS_Y    124
#define ROW_H     42
#define ROW_GAP   8
#define ROW_STEP  (ROW_H + ROW_GAP)

#define TX_LOW_DBM     3
#define TX_MAX_DBM     9
#define INIT_CONNECTED 3

#define COL_DIM   0x8A8594
#define COL_RAISE 0x170A28

enum {
  R_RANDOMIZE = 0,
  R_TXPOWER,
  R_DISCONNECT,
  R_COUNT,
};

typedef struct {
  const char *sym;
  const char *name;
} radio_row_def_t;

static const radio_row_def_t ROW_DEFS[R_COUNT] = {
    {LV_SYMBOL_REFRESH, "Randomize MAC"},
    {LV_SYMBOL_CHARGE, "Max TX Power"},
    {LV_SYMBOL_POWER, "Disconnect All"},
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_card_mac = NULL;
static lv_obj_t *s_card_conn = NULL;
static lv_obj_t *s_row[R_COUNT];
static lv_obj_t *s_icon_lbl[R_COUNT];
static lv_obj_t *s_val_lbl[R_COUNT];
static lv_timer_t *s_nav_timer = NULL;

static char s_mac[18];
static int s_connected = INIT_CONNECTED;
static int s_tx_dbm = TX_LOW_DBM;
static int s_sel = 0;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static void nav_timer_cb(lv_timer_t *t);

static void gen_mac(void) {
  snprintf(s_mac,
           sizeof(s_mac),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF));
}

static void update_card(void) {
  if (s_card_mac != NULL)
    lv_label_set_text_fmt(s_card_mac, "MAC: %s", s_mac);
  if (s_card_conn != NULL)
    lv_label_set_text_fmt(s_card_conn, "Connected: %d", s_connected);
}

static void update_values(void) {
  lv_label_set_text_fmt(s_val_lbl[R_TXPOWER], "+%d dBm", s_tx_dbm);
  lv_label_set_text_fmt(s_val_lbl[R_DISCONNECT], "%d", s_connected);
  lv_label_set_text(s_val_lbl[R_RANDOMIZE], "");
}

static void refresh_selection(void) {
  const lv_color_t accent = current_theme.border_accent;
  const lv_color_t dim = lv_color_hex(COL_DIM);
  for (int i = 0; i < R_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(s_row[i], sel ? accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(s_row[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        s_row[i], sel ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
    lv_obj_set_style_shadow_width(s_row[i], sel ? 16 : 0, 0);
    lv_obj_set_style_shadow_color(s_row[i], accent, 0);
    lv_obj_set_style_shadow_spread(s_row[i], sel ? -3 : 0, 0);
    lv_obj_set_style_text_color(s_icon_lbl[i], sel ? accent : dim, 0);
    lv_obj_set_style_text_color(s_val_lbl[i], sel ? accent : dim, 0);
  }
}

static lv_obj_t *lit_card(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, CONTENT_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CARD_Y);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);
  lv_obj_set_style_pad_hor(card, 12, 0);
  lv_obj_set_style_pad_ver(card, 8, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, 4, 0);
  return card;
}

static lv_obj_t *make_row(lv_obj_t *parent, int i) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, CONTENT_W, ROW_H);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, ROWS_Y + i * ROW_STEP);
  lv_obj_set_style_radius(row, 10, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_left(row, 12, 0);
  lv_obj_set_style_pad_right(row, 12, 0);
  lv_obj_set_style_pad_ver(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ic = lv_label_create(row);
  lv_label_set_text(ic, ROW_DEFS[i].sym);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_16, 0);
  lv_obj_set_width(ic, 22);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, ROW_DEFS[i].name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_pad_left(name, 8, 0);
  lv_obj_set_flex_grow(name, 1);

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, "");
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);

  s_icon_lbl[i] = ic;
  s_val_lbl[i] = val;
  return row;
}

static void do_action(int idx) {
  switch (idx) {
    case R_RANDOMIZE:
      gen_mac();
      update_card();
      ui_feedback(UI_FB_WRITE);
      notify(NOTIFY_INFO, "MAC randomized");
      break;
    case R_TXPOWER:
      s_tx_dbm = TX_MAX_DBM;
      update_values();
      ui_feedback(UI_FB_WRITE);
      notify(NOTIFY_SAVED, "TX power maxed");
      break;
    case R_DISCONNECT:
      s_connected = 0;
      update_card();
      update_values();
      ui_feedback(UI_FB_WRITE);
      notify(NOTIFY_WARNING, "All devices dropped");
      break;
    default:
      break;
  }
  refresh_selection();
}

void ui_ble_radio_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_connected = INIT_CONNECTED;
  s_tx_dbm = TX_LOW_DBM;
  s_sel = 0;
  s_up_last = false;
  s_down_last = false;
  s_left_last = false;
  s_ok_last = false;
  s_back_last = false;
  gen_mac();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "BLE RADIO", RADIO_ICON);
  ui_chrome_footer(s_screen, "UP/DOWN choose   OK do   BACK back");

  lv_obj_t *card = lit_card(s_screen);

  s_card_mac = lv_label_create(card);
  lv_obj_set_style_text_color(s_card_mac, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_card_mac, &lv_font_montserrat_14, 0);

  s_card_conn = lv_label_create(card);
  lv_obj_set_style_text_color(s_card_conn, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_card_conn, &lv_font_montserrat_12, 0);

  for (int i = 0; i < R_COUNT; i++)
    s_row[i] = make_row(s_screen, i);

  update_card();
  update_values();
  refresh_selection();

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

  if (ui_input_is_locked()) {
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_BLE_MENU);
    return;
  }
  if (down && !s_down_last) {
    s_sel = (s_sel + 1) % R_COUNT;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (up && !s_up_last) {
    s_sel = (s_sel - 1 + R_COUNT) % R_COUNT;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (ok && !s_ok_last)
    do_action(s_sel);

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_ok_last = ok;
  s_back_last = back;
}

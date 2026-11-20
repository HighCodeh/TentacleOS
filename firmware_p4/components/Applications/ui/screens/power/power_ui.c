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

#include "power_ui.h"

#include <stdio.h>

#include "i2c_init.h"
#include "lvgl.h"

#include "bq25896.h"
#include "battery_service.h"
#include "msgbox_ui.h"
#include "power_policy.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "menu_component_ui.h"
#include "esp_attr.h"

#define REFRESH_MS 700

#define HEADER_ICON "/assets/icons/power_settings_new.bin"

#define HERO_Y        46
#define HERO_H        74
#define HERO_W_PCT    92
#define HERO_RADIUS   12
#define HERO_BORDER_W 1
#define GLOW_W        14
#define GLOW_SPREAD   -3
#define CHIP_PAD_H    9
#define CHIP_PAD_V    2
#define CHIP_BORDER_W 1

#define PCT_X   16
#define PCT_Y   -11
#define VOLT_Y  13
#define STATE_X -12
#define STATE_Y -11
#define SRC_X   -12
#define SRC_Y   13

#define LIST_Y       128
#define LIST_MARGIN  8
#define LIST_GUTTER  24
#define ROW_H        34
#define ROW_GAP      5
#define ROW_RADIUS   9
#define ROW_BORDER_W 2
#define ROW_PAD_H    10
#define ICON_W       20
#define NAME_PAD_L   9

#define COL_RAISE 0x170A28

enum { ACT_CHARGE, ACT_SCAN, ACT_REGS, ACT_OFF, ACT_COUNT };

static const char *const ACT_ICON[ACT_COUNT] = {
    LV_SYMBOL_CHARGE, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS, LV_SYMBOL_POWER};
static const char *const ACT_NAMES[ACT_COUNT] = {"Charging", "I2C Scan", "Registers", "Power Off"};

static lv_obj_t *s_screen = NULL;
EXT_RAM_BSS_ATTR static menu_component_t s_scroll;
static lv_obj_t *s_pct = NULL;
static lv_obj_t *s_volt = NULL;
static lv_obj_t *s_state = NULL;
static lv_obj_t *s_src = NULL;
static lv_obj_t *s_row[ACT_COUNT];
static lv_obj_t *s_row_icon[ACT_COUNT];
static lv_obj_t *s_row_val[ACT_COUNT];
static lv_timer_t *s_timer = NULL;
static int s_sel = 0;
static char s_scan_msg[96];

static const char *vbus_name(bq25896_vbus_status_t s) {
  switch (s) {
    case VBUS_STATUS_USB_HOST:
      return "USB";
    case VBUS_STATUS_ADAPTER_PORT:
      return "Adapter";
    case VBUS_STATUS_OTG:
      return "OTG";
    default:
      return "None";
  }
}

static void style_state_chip(bool charging) {
  if (charging) {
    lv_obj_set_style_text_color(s_state, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_obj_set_style_bg_color(s_state, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_obj_set_style_bg_opa(s_state, LV_OPA_20, 0);
    lv_obj_set_style_border_width(s_state, 0, 0);
  } else {
    lv_obj_set_style_text_color(s_state, current_theme.text_secondary, 0);
    lv_obj_set_style_bg_color(s_state, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(s_state, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_state, CHIP_BORDER_W, 0);
    lv_obj_set_style_border_color(s_state, current_theme.border_inactive, 0);
  }
}

static void refresh_telem(void) {
  battery_charge_snapshot_t charge;
  if (!battery_service_get_charge(&charge)) {
    lv_label_set_text(s_pct, "--%");
    lv_label_set_text(s_volt, "-- V");
    lv_label_set_text(s_src, "Reading battery");
    lv_label_set_text(s_state, "Unavailable");
    style_state_chip(false);
    lv_label_set_text(s_row_val[ACT_CHARGE], "--");
    return;
  }
  const bq25896_telem_t *t = &charge.charger;
  if (charge.soc >= 0)
    lv_label_set_text_fmt(s_pct, "%s%d%%", charge.gauge_valid ? "" : "~", charge.soc);
  else
    lv_label_set_text(s_pct, "--%");
  lv_label_set_text_fmt(s_volt, "%u.%02u V", t->vbat_mv / 1000, (t->vbat_mv % 1000) / 10);
  if (t->power_good)
    lv_label_set_text_fmt(
        s_src, "%s %u.%02u V", vbus_name(t->vbus), t->vbus_mv / 1000, (t->vbus_mv % 1000) / 10);
  else
    lv_label_set_text(s_src, "On battery");
  const char *status = !t->power_good                        ? "On battery"
                       : t->charging                         ? "Charging"
                       : t->chg == CHARGE_STATUS_CHARGE_DONE ? "Fully charged"
                       : !charge.requested                   ? "Charge paused"
                                                             : "Charge blocked";
  lv_label_set_text(s_state, status);
  style_state_chip(t->charging);
  lv_label_set_text(s_row_val[ACT_CHARGE], charge.requested ? "ON" : "OFF");
}

static void refresh_selection(void) {
  const lv_color_t accent = current_theme.border_accent;
  const lv_color_t dim = current_theme.text_secondary;
  for (int i = 0; i < ACT_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(s_row[i], sel ? accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(s_row[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        s_row[i], sel ? current_theme.bg_item_top : current_theme.bg_secondary, 0);
    lv_obj_set_style_shadow_width(s_row[i], sel ? GLOW_W : 0, 0);
    lv_obj_set_style_shadow_spread(s_row[i], sel ? GLOW_SPREAD : 0, 0);
    lv_obj_set_style_text_color(s_row_icon[i], sel ? accent : dim, 0);
    lv_obj_set_style_text_color(s_row_val[i], sel ? accent : dim, 0);
  }
  menu_component_scroll_to_item(&s_scroll, s_row[s_sel]);
}

static void build_hero(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(card, lv_pct(HERO_W_PCT));
  lv_obj_set_height(card, HERO_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, HERO_Y);
  lv_obj_set_style_radius(card, HERO_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, HERO_BORDER_W, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, GLOW_W, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_spread(card, GLOW_SPREAD, 0);
  lv_obj_set_style_pad_all(card, 0, 0);

  s_pct = lv_label_create(card);
  lv_label_set_text(s_pct, "--");
  lv_obj_set_style_text_font(s_pct, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_pct, current_theme.border_accent, 0);
  lv_obj_align(s_pct, LV_ALIGN_LEFT_MID, PCT_X, PCT_Y);

  s_volt = lv_label_create(card);
  lv_label_set_text(s_volt, "-- V");
  lv_obj_set_style_text_font(s_volt, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_volt, current_theme.text_main, 0);
  lv_obj_align(s_volt, LV_ALIGN_LEFT_MID, PCT_X, VOLT_Y);

  s_state = lv_label_create(card);
  lv_label_set_text(s_state, "--");
  lv_obj_set_style_text_font(s_state, &lv_font_montserrat_12, 0);
  lv_obj_set_style_radius(s_state, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(s_state, CHIP_PAD_H, 0);
  lv_obj_set_style_pad_ver(s_state, CHIP_PAD_V, 0);
  lv_obj_align(s_state, LV_ALIGN_RIGHT_MID, STATE_X, STATE_Y);
  style_state_chip(false);

  s_src = lv_label_create(card);
  lv_label_set_text(s_src, "");
  lv_obj_set_style_text_font(s_src, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_src, current_theme.text_secondary, 0);
  lv_obj_align(s_src, LV_ALIGN_RIGHT_MID, SRC_X, SRC_Y);
}

static lv_obj_t *make_row(lv_obj_t *parent, int i) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, lv_pct(100), ROW_H);
  lv_obj_set_style_radius(row, ROW_RADIUS, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, ROW_BORDER_W, 0);
  lv_obj_set_style_pad_left(row, ROW_PAD_H, 0);
  lv_obj_set_style_pad_right(row, ROW_PAD_H, 0);
  lv_obj_set_style_pad_top(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 0, 0);
  lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ic = lv_label_create(row);
  lv_label_set_text(ic, ACT_ICON[i]);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0);
  lv_obj_set_width(ic, ICON_W);
  lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, ACT_NAMES[i]);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_pad_left(name, NAME_PAD_L, 0);
  lv_obj_set_width(name, 0);
  lv_obj_set_flex_grow(name, 1);
  lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, i == ACT_CHARGE ? "--" : LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);

  s_row_icon[i] = ic;
  s_row_val[i] = val;
  return row;
}

static void build_actions(void) {
  lv_obj_t *list = lv_obj_create(s_screen);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(
      list, ui_screen_w() - LIST_GUTTER, ui_screen_h() - LIST_Y - UI_CHROME_FOOTER_H - 4);
  lv_obj_align(list, LV_ALIGN_TOP_LEFT, LIST_MARGIN, LIST_Y);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, ROW_GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  for (int i = 0; i < ACT_COUNT; i++)
    s_row[i] = make_row(list, i);
  menu_component_attach_scroll(&s_scroll, s_screen, list);
}

static void i2c_scan_fill(void) {
  int n = 0;
  s_scan_msg[0] = '\0';
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    esp_err_t r = i2c_master_probe(i2c_get_bus(), a, 20);
    if (r == ESP_OK && n < (int)sizeof(s_scan_msg) - 7)
      n += snprintf(s_scan_msg + n, sizeof(s_scan_msg) - n, "0x%02X ", a);
  }
  if (n == 0)
    snprintf(s_scan_msg, sizeof(s_scan_msg), "No I2C devices found");
}

static void do_action(int act) {
  if (act == ACT_CHARGE) {
    battery_charge_snapshot_t charge;
    if (!battery_service_get_charge(&charge) ||
        battery_service_request_charge(!charge.requested) != ESP_OK)
      msgbox_open(LV_SYMBOL_WARNING, "Charge request failed", NULL, NULL, NULL);
    refresh_telem();
  } else if (act == ACT_SCAN) {
    i2c_scan_fill();
    msgbox_open(LV_SYMBOL_LIST, s_scan_msg, NULL, NULL, NULL);
  } else if (act == ACT_REGS) {
    uint8_t ts = bq25896_reg_raw(0x10) & 0x7F;
    int tsx10 = 210 + ts * 465 / 100;
    uint8_t f = bq25896_reg_raw(0x0C);
    const char *ts_hint = (tsx10 < 344)   ? "TS low: short? (Hot)"
                          : (tsx10 > 732) ? "TS high: open? (Cold)"
                                          : "TS in range (OK)";
    snprintf(s_scan_msg,
             sizeof(s_scan_msg),
             "TS %d.%d%% (~50 ok)\n%s\n0B:%02X 0C:%02X NTC:%X\n00:%02X 04:%02X "
             "0D:%02X",
             tsx10 / 10,
             tsx10 % 10,
             ts_hint,
             bq25896_reg_raw(0x0B),
             f,
             f & 0x07,
             bq25896_reg_raw(0x00),
             bq25896_reg_raw(0x04),
             bq25896_reg_raw(0x0D));
    msgbox_open(LV_SYMBOL_SETTINGS, s_scan_msg, NULL, NULL, NULL);
  } else if (act == ACT_OFF) {
    power_policy_show_shutdown_dialog();
  }
}

static void power_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % ACT_COUNT;
        refresh_selection();
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel - 1 + ACT_COUNT) % ACT_COUNT;
        refresh_selection();
      }
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press)
        do_action(s_sel);
      break;
    default:
      break;
  }
}

static void refresh_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  refresh_telem();
}

void ui_power_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "Power", HEADER_ICON);

  build_hero();
  build_actions();

  refresh_selection();
  refresh_telem();

  ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " Select  OK Run  BACK");

  if (s_timer == NULL)
    s_timer = lv_timer_create(refresh_cb, REFRESH_MS, NULL);

  ui_input_set_screen_handler(power_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

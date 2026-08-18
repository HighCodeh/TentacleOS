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

#include "wifi_hotspot_ui.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"
#include "st7789.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"
#include "wifi_service.h"

#define UPTIME_TICK_MS 1000

#define HDR_TITLE   "HOTSPOT (AP)"
#define HDR_ICON    NULL
#define FOOTER_HINT "UP/DOWN  OK TOGGLE  BACK"

#define MX        8
#define CONTENT_W (LCD_H_RES - 2 * MX)
#define BODY_TOP  UI_CHROME_HEADER_H
#define BODY_H    (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define STACK_GAP 9

#define STATUS_H      54
#define STATUS_RADIUS 11
#define STATUS_PAD_H  10
#define STATUS_PAD_V  8
#define STATUS_GAP    8
#define DOT_SIZE      11
#define TOGGLE_W      38
#define TOGGLE_H      18
#define TOGGLE_RADIUS 10
#define KNOB_SIZE     14
#define KNOB_INSET    3

#define RING_H       92
#define RING_GAP     12
#define ARC_SIZE     76
#define ARC_WIDTH    6
#define ARC_ROTATION 270
#define ARC_MIN      0
#define ARC_MAX      8
#define ARC_VAL      0
#define ARC_TRACK    0x241F31

#define TILE_H      43
#define TILE_RADIUS 8
#define TILE_PAD_H  8
#define TILE_PAD_V  4
#define TILE_GAP    6
#define RCOL_GAP    6

#define COL_CYAN 0x37E0A8
#define COL_ACC2 0xB89AFF

#define AP_NAME           "HighBoy_AP"
#define AP_PASSWORD       NULL
#define AP_MAX_CONN       8
#define AP_IP             "192.168.4.1"
#define UPTIME_ONLINE_FMT "AP ONLINE - %02d:%02d"
#define TXT_OFFLINE       "AP OFFLINE"
#define UPTIME_BUF        40
#define START_SECS        0

#define TASK_STACK_SIZE 4096
#define TASK_PRIORITY   SYS_PRIO_SERVICE_LO

#define VAL_PLACEHOLDER "--"

#define LBL_CHANNEL "CHANNEL"
#define VAL_CHANNEL "-"
#define LBL_GATEWAY "GATEWAY"
#define VAL_GATEWAY AP_IP
#define LBL_TX      "TX"
#define LBL_RX      "RX"
#define ARC_CENTER  "-"
#define ARC_SUB     "/ 8"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_dot = NULL;
static lv_obj_t *s_toggle = NULL;
static lv_obj_t *s_knob = NULL;
static lv_obj_t *s_uptime = NULL;
static lv_timer_t *s_uptime_timer = NULL;

static int s_secs = START_SECS;
static bool s_online = true;

static volatile bool s_ap_busy = false;
static volatile bool s_ap_desired = false;
static volatile bool s_ap_dirty = false;

static void ap_apply_task(void *arg) {
  (void)arg;
  do {
    bool want = s_ap_desired;
    s_ap_dirty = false;
    if (want) {
      wifi_service_save_ap_config(AP_NAME, AP_PASSWORD, AP_MAX_CONN, AP_IP, true);
      wifi_service_start();
    } else {
      wifi_service_set_enabled(false);
    }
  } while (s_ap_dirty);
  s_ap_busy = false;
  vTaskDelete(NULL);
}

static void ap_request(bool enabled) {
  s_ap_desired = enabled;
  s_ap_dirty = true;
  if (!s_ap_busy) {
    s_ap_busy = true;
    if (xTaskCreatePinnedToCore(
            ap_apply_task, "ap_ctl", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_ap_busy = false;
    }
  }
}

static lv_obj_t *
make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

static lv_obj_t *make_tile(
    lv_obj_t *parent, const char *lbl, const char *val, lv_color_t vcol, const lv_font_t *vfont) {
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(tile, TILE_RADIUS, 0);
  lv_obj_set_style_bg_color(tile, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(tile, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(tile, 1, 0);
  lv_obj_set_style_pad_hor(tile, TILE_PAD_H, 0);
  lv_obj_set_style_pad_ver(tile, TILE_PAD_V, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(tile, 1, 0);

  make_label(tile, lbl, &lv_font_montserrat_12, current_theme.text_secondary);
  make_label(tile, val, vfont, vcol);
  return tile;
}

static void update_uptime_label(void) {
  if (s_uptime == NULL)
    return;
  if (s_online) {
    char buf[UPTIME_BUF];
    snprintf(buf, sizeof(buf), UPTIME_ONLINE_FMT, s_secs / 60, s_secs % 60);
    lv_label_set_text(s_uptime, buf);
    lv_obj_set_style_text_color(s_uptime, lv_color_hex(UI_COL_SUCCESS), 0);
  } else {
    lv_label_set_text(s_uptime, TXT_OFFLINE);
    lv_obj_set_style_text_color(s_uptime, current_theme.text_secondary, 0);
  }
}

static void apply_online(void) {
  lv_color_t c = s_online ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_secondary;

  if (s_dot != NULL)
    lv_obj_set_style_bg_color(s_dot, c, 0);

  if (s_toggle != NULL) {
    lv_obj_set_style_bg_color(s_toggle, c, 0);
    lv_obj_set_style_bg_opa(s_toggle, s_online ? LV_OPA_20 : LV_OPA_10, 0);
    lv_obj_set_style_border_color(s_toggle, c, 0);
  }
  if (s_knob != NULL) {
    lv_obj_set_style_bg_color(s_knob, c, 0);
    lv_obj_align(s_knob,
                 s_online ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                 s_online ? -KNOB_INSET : KNOB_INSET,
                 0);
  }
  update_uptime_label();
}

static void stop_uptime_timer(void) {
  if (s_uptime_timer != NULL) {
    lv_timer_delete(s_uptime_timer);
    s_uptime_timer = NULL;
  }
}

static void uptime_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_uptime_timer = NULL;
    return;
  }
  if (!s_online)
    return;
  s_secs++;
  update_uptime_label();
}

static void build_status_card(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, LV_PCT(100), STATUS_H);
  lv_obj_set_style_radius(card, STATUS_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_color(card, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_hor(card, STATUS_PAD_H, 0);
  lv_obj_set_style_pad_ver(card, STATUS_PAD_V, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, STATUS_GAP, 0);

  s_dot = lv_obj_create(card);
  lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_dot, DOT_SIZE, DOT_SIZE);
  lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_dot, 0, 0);
  lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_dot, lv_color_hex(UI_COL_SUCCESS), 0);

  lv_obj_t *txt = lv_obj_create(card);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_height(txt, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(txt, 1);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(txt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(txt, 2, 0);

  make_label(txt, AP_NAME, &lv_font_montserrat_14, current_theme.text_main);
  s_uptime = make_label(txt, "", &lv_font_montserrat_12, lv_color_hex(UI_COL_SUCCESS));

  s_toggle = lv_obj_create(card);
  lv_obj_remove_flag(s_toggle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_toggle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_toggle, TOGGLE_W, TOGGLE_H);
  lv_obj_set_style_radius(s_toggle, TOGGLE_RADIUS, 0);
  lv_obj_set_style_pad_all(s_toggle, 0, 0);
  lv_obj_set_style_bg_color(s_toggle, lv_color_hex(UI_COL_SUCCESS), 0);
  lv_obj_set_style_bg_opa(s_toggle, LV_OPA_20, 0);
  lv_obj_set_style_border_color(s_toggle, lv_color_hex(UI_COL_SUCCESS), 0);
  lv_obj_set_style_border_width(s_toggle, 1, 0);

  s_knob = lv_obj_create(s_toggle);
  lv_obj_remove_flag(s_knob, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_knob, KNOB_SIZE, KNOB_SIZE);
  lv_obj_set_style_radius(s_knob, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_knob, 0, 0);
  lv_obj_set_style_bg_opa(s_knob, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_knob, lv_color_hex(UI_COL_SUCCESS), 0);
  lv_obj_align(s_knob, LV_ALIGN_RIGHT_MID, -KNOB_INSET, 0);
}

static void build_arc(lv_obj_t *parent) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
  lv_arc_set_rotation(arc, ARC_ROTATION);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, ARC_MIN, ARC_MAX);
  lv_arc_set_value(arc, ARC_VAL);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(ARC_TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

  lv_obj_t *center = make_label(arc, ARC_CENTER, &lv_font_montserrat_14, current_theme.text_main);
  lv_obj_align(center, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t *sub = make_label(arc, ARC_SUB, &lv_font_montserrat_12, current_theme.text_secondary);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 11);
}

static void build_ring_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), RING_H);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, RING_GAP, 0);

  build_arc(row);

  lv_obj_t *rcol = lv_obj_create(row);
  lv_obj_remove_flag(rcol, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rcol, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_height(rcol, RING_H);
  lv_obj_set_flex_grow(rcol, 1);
  lv_obj_set_style_bg_opa(rcol, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(rcol, 0, 0);
  lv_obj_set_style_pad_all(rcol, 0, 0);
  lv_obj_set_flex_flow(rcol, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(rcol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(rcol, RCOL_GAP, 0);

  lv_obj_t *ch =
      make_tile(rcol, LBL_CHANNEL, VAL_CHANNEL, current_theme.text_main, &lv_font_montserrat_14);
  lv_obj_set_size(ch, LV_PCT(100), TILE_H);
  lv_obj_t *gw =
      make_tile(rcol, LBL_GATEWAY, VAL_GATEWAY, current_theme.text_main, &lv_font_montserrat_12);
  lv_obj_set_size(gw, LV_PCT(100), TILE_H);
}

static void build_traffic_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), TILE_H);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(row, TILE_GAP, 0);

  lv_obj_t *tx =
      make_tile(row, LBL_TX, VAL_PLACEHOLDER, lv_color_hex(COL_CYAN), &lv_font_montserrat_14);
  lv_obj_set_height(tx, TILE_H);
  lv_obj_set_flex_grow(tx, 1);
  lv_obj_t *rx =
      make_tile(row, LBL_RX, VAL_PLACEHOLDER, lv_color_hex(COL_ACC2), &lv_font_montserrat_14);
  lv_obj_set_height(rx, TILE_H);
  lv_obj_set_flex_grow(rx, 1);
}

static void wifi_hotspot_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        stop_uptime_timer();
        ap_request(false);
        ui_switch_screen(SCREEN_WIFI_MENU);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        s_online = !s_online;
        apply_online();
        ap_request(s_online);
        ui_feedback(UI_FB_SELECT);
      }
      break;
    default:
      break;
  }
}

void ui_wifi_hotspot_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  stop_uptime_timer();
  s_dot = NULL;
  s_toggle = NULL;
  s_knob = NULL;
  s_uptime = NULL;
  s_secs = START_SECS;
  s_online = true;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);
  ui_chrome_footer(s_screen, FOOTER_HINT);

  lv_obj_t *stack = lv_obj_create(s_screen);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(stack, CONTENT_W, BODY_H);
  lv_obj_align(stack, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(stack, 0, 0);
  lv_obj_set_style_pad_all(stack, 0, 0);
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(stack, STACK_GAP, 0);

  build_status_card(stack);
  build_ring_row(stack);
  build_traffic_row(stack);

  apply_online();

  ap_request(true);

  ui_input_set_screen_handler(wifi_hotspot_input, NULL);
  s_uptime_timer = lv_timer_create(uptime_tick_cb, UPTIME_TICK_MS, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

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

#include "usb_mouse_ui.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "tusb.h"

#include "bad_usb.h"
#include "hid_hal.h"
#include "tusb_desc.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define MOVE_TICK_MS    40
#define JIGGLE_TICK_MS  70
#define JIGGLE_STEP_DEG 18
#define JIGGLE_RADIUS   16

#define HDR_TITLE  "HID MOUSE"
#define HDR_ICON   "/assets/icons/mouse.bin"
#define FOOTER_TXT "ARROWS move  OK click/hold R  OK+UP/DN scroll"

#define RIGHT_CLICK_HOLD_MS 700
#define SCROLL_REPEAT_MS    140

#define MX        8
#define CONTENT_W (240 - 2 * MX)

#define COL_ACC2 0xB89AFF

#define STATUS_Y 48
#define STATUS_H 18

#define PAD_Y   71
#define PAD_W   (CONTENT_W - RAIL_W - ROW_GAP)
#define PAD_H   148
#define RAIL_W  26
#define ROW_GAP 6

#define CLICK_Y 226
#define CLICK_H 34
#define CLICK_W ((CONTENT_W - ROW_GAP) / 2)

#define CUR_W       16
#define CUR_H       22
#define CUR_MIN     4
#define CUR_MAX_X   (PAD_W - CUR_W - 6)
#define CUR_MAX_Y   (PAD_H - CUR_H - 6)
#define MOVE_STEP   12
#define CUR_START_X 112
#define CUR_START_Y 66

#define JIGGLE_ON  "JIGGLE ON"
#define JIGGLE_OFF "JIGGLE OFF"

static const lv_point_precise_t CURSOR_PTS[] = {
    {1, 1}, {1, 20}, {6, 15}, {10, 22}, {13, 21}, {8, 13}, {16, 13}, {1, 1}};
#define CURSOR_PT_COUNT ((int)(sizeof(CURSOR_PTS) / sizeof(CURSOR_PTS[0])))

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_move_timer = NULL;
static lv_timer_t *s_jiggle_timer = NULL;

static lv_obj_t *s_cursor = NULL;
static lv_obj_t *s_chip = NULL;
static lv_obj_t *s_chip_lbl = NULL;
static lv_obj_t *s_status_dot = NULL;
static lv_obj_t *s_status_lbl = NULL;

static int s_cur_x = CUR_START_X;
static int s_cur_y = CUR_START_Y;
static int s_jig_ang = 0;
static bool s_jiggle = false;
static bool s_prev_mux_native = false;
static bool s_usb_init_ok = false;
static bool s_usb_ready = false;
static bool s_ok_last = false;
static int64_t s_ok_down_ms = 0;
static int64_t s_last_scroll_ms = 0;
static bool s_right_click_sent = false;
static bool s_ok_chord = false;
static bool s_jiggle_chord_latched = false;

static int clamp(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static void update_cursor(void) {
  s_cur_x = clamp(s_cur_x, CUR_MIN, CUR_MAX_X);
  s_cur_y = clamp(s_cur_y, CUR_MIN, CUR_MAX_Y);
  if (s_cursor)
    lv_obj_set_pos(s_cursor, s_cur_x, s_cur_y);
}

static void refresh_chip(void) {
  if (s_chip_lbl)
    lv_label_set_text(s_chip_lbl, s_jiggle ? JIGGLE_ON : JIGGLE_OFF);
  if (s_chip) {
    lv_obj_set_style_bg_opa(s_chip, s_jiggle ? LV_OPA_COVER : LV_OPA_30, 0);
  }
  if (s_chip_lbl)
    lv_obj_set_style_text_color(
        s_chip_lbl, s_jiggle ? current_theme.screen_base : current_theme.text_secondary, 0);
}

static void refresh_usb_status(void) {
  bool ready = s_usb_init_ok && tud_mounted();
  if (ready == s_usb_ready && s_status_lbl != NULL)
    return;
  s_usb_ready = ready;

  const char *text = !s_usb_init_ok ? "USB HID ERROR" : ready ? "USB HID READY" : "CONNECT USB...";
  lv_color_t color = !s_usb_init_ok ? lv_color_hex(UI_COL_DANGER)
                     : ready        ? lv_color_hex(UI_COL_SUCCESS)
                                    : current_theme.border_accent;
  if (s_status_lbl != NULL) {
    lv_label_set_text(s_status_lbl, text);
    lv_obj_set_style_text_color(s_status_lbl, color, 0);
  }
  if (s_status_dot != NULL)
    lv_obj_set_style_bg_color(s_status_dot, color, 0);
}

static void build_status(void) {
  s_status_dot = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_status_dot, 7, 7);
  lv_obj_align(s_status_dot, LV_ALIGN_TOP_LEFT, MX, STATUS_Y + 5);
  lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_status_dot, 0, 0);
  lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);

  s_status_lbl = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_TOP_LEFT, MX + 13, STATUS_Y + 1);

  s_chip = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_chip, 72, STATUS_H);
  lv_obj_align(s_chip, LV_ALIGN_TOP_RIGHT, -MX, STATUS_Y - 1);
  lv_obj_set_style_radius(s_chip, 6, 0);
  lv_obj_set_style_pad_all(s_chip, 0, 0);
  lv_obj_set_style_bg_color(s_chip, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_chip, 0, 0);

  s_chip_lbl = lv_label_create(s_chip);
  lv_obj_set_style_text_font(s_chip_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(s_chip_lbl);
  refresh_chip();
  refresh_usb_status();
}

static void build_trackpad(void) {
  lv_obj_t *pad = lv_obj_create(s_screen);
  lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pad, PAD_W, PAD_H);
  lv_obj_align(pad, LV_ALIGN_TOP_LEFT, MX, PAD_Y);
  lv_obj_set_style_radius(pad, 10, 0);
  lv_obj_set_style_pad_all(pad, 0, 0);
  lv_obj_set_style_bg_color(pad, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(pad, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(pad, 1, 0);
  lv_obj_set_style_border_opa(pad, LV_OPA_70, 0);

  lv_obj_t *vline = lv_obj_create(pad);
  lv_obj_remove_flag(vline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(vline, 1, PAD_H - 24);
  lv_obj_align(vline, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_border_width(vline, 0, 0);
  lv_obj_set_style_bg_color(vline, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(vline, LV_OPA_20, 0);

  lv_obj_t *hline = lv_obj_create(pad);
  lv_obj_remove_flag(hline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hline, PAD_W - 24, 1);
  lv_obj_align(hline, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_border_width(hline, 0, 0);
  lv_obj_set_style_bg_color(hline, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(hline, LV_OPA_20, 0);

  s_cursor = lv_line_create(pad);
  lv_line_set_points(s_cursor, CURSOR_PTS, CURSOR_PT_COUNT);
  lv_obj_set_style_line_width(s_cursor, 2, 0);
  lv_obj_set_style_line_color(s_cursor, lv_color_hex(COL_ACC2), 0);
  lv_obj_set_style_line_rounded(s_cursor, false, 0);
  update_cursor();

  lv_obj_t *tag = lv_label_create(pad);
  lv_label_set_text(tag, "trackpad");
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tag, current_theme.text_secondary, 0);
  lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 6, -4);
}

static void build_rail(void) {
  lv_obj_t *rail = lv_obj_create(s_screen);
  lv_obj_remove_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(rail, RAIL_W, PAD_H);
  lv_obj_align(rail, LV_ALIGN_TOP_RIGHT, -MX, PAD_Y);
  lv_obj_set_style_radius(rail, 8, 0);
  lv_obj_set_style_pad_ver(rail, 8, 0);
  lv_obj_set_style_pad_hor(rail, 0, 0);
  lv_obj_set_style_bg_color(rail, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(rail, current_theme.text_secondary, 0);
  lv_obj_set_style_border_opa(rail, LV_OPA_40, 0);
  lv_obj_set_style_border_width(rail, 1, 0);
  lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      rail, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *up = lv_label_create(rail);
  lv_label_set_text(up, LV_SYMBOL_UP);
  lv_obj_set_style_text_font(up, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(up, current_theme.border_accent, 0);

  lv_obj_t *scr = lv_label_create(rail);
  lv_label_set_text(scr, "SCR");
  lv_obj_set_style_text_font(scr, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(scr, current_theme.text_secondary, 0);

  lv_obj_t *down = lv_label_create(rail);
  lv_label_set_text(down, LV_SYMBOL_DOWN);
  lv_obj_set_style_text_font(down, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(down, current_theme.border_accent, 0);
}

static void build_click(int x, const char *text, bool selected) {
  lv_obj_t *btn = lv_obj_create(s_screen);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(btn, CLICK_W, CLICK_H);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, CLICK_Y);
  lv_obj_set_style_radius(btn, 9, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_set_style_bg_color(btn, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(
      btn, selected ? current_theme.border_accent : current_theme.border_inactive, 0);
  lv_obj_set_style_border_opa(btn, selected ? LV_OPA_COVER : LV_OPA_40, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  if (selected) {
    lv_obj_set_style_shadow_width(btn, 12, 0);
    lv_obj_set_style_shadow_color(btn, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_spread(btn, -3, 0);
  }

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(
      lbl, selected ? current_theme.border_accent : current_theme.text_secondary, 0);
  lv_obj_center(lbl);
}

static void jiggle_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_jiggle_timer = NULL;
    return;
  }
  refresh_usb_status();
  if (!s_jiggle || !s_usb_ready)
    return;
  s_jig_ang = (s_jig_ang + JIGGLE_STEP_DEG) % 360;
  int cx = (CUR_MIN + CUR_MAX_X) / 2;
  int cy = (CUR_MIN + CUR_MAX_Y) / 2;
  int old_x = s_cur_x;
  int old_y = s_cur_y;
  s_cur_x = cx + (JIGGLE_RADIUS * lv_trigo_sin((int16_t)s_jig_ang)) / 32767;
  s_cur_y = cy + (JIGGLE_RADIUS * lv_trigo_cos((int16_t)s_jig_ang)) / 32767;
  update_cursor();
  hid_hal_mouse_move((int8_t)(s_cur_x - old_x), (int8_t)(s_cur_y - old_y));
}

static void usb_mouse_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_BADUSB_MENU);
      break;
    default:
      break;
  }
}

static void move_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_move_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  refresh_usb_status();

  bool up = input_is_down(INPUT_BTN_UP);
  bool down = input_is_down(INPUT_BTN_DOWN);
  bool left = input_is_down(INPUT_BTN_LEFT);
  bool right = input_is_down(INPUT_BTN_RIGHT);
  bool ok = input_is_down(INPUT_BTN_OK);
  int64_t now_ms = esp_timer_get_time() / 1000;

  if (ok && !s_ok_last) {
    s_ok_down_ms = now_ms;
    s_last_scroll_ms = 0;
    s_right_click_sent = false;
    s_ok_chord = false;
    s_jiggle_chord_latched = false;
  }

  // OK+LEFT toggles the keep-awake circle without producing a click.
  if (ok && left && !s_jiggle_chord_latched) {
    s_jiggle_chord_latched = true;
    s_ok_chord = true;
    s_jiggle = !s_jiggle;
    refresh_chip();
    ui_feedback(UI_FB_SELECT);
  } else if (ok && (up || down)) {
    s_ok_chord = true;
    if (s_usb_ready && (s_last_scroll_ms == 0 || now_ms - s_last_scroll_ms >= SCROLL_REPEAT_MS)) {
      s_last_scroll_ms = now_ms;
      hid_hal_mouse_scroll(up ? 1 : -1);
    }
  } else if (ok && !s_right_click_sent && !s_ok_chord &&
             now_ms - s_ok_down_ms >= RIGHT_CLICK_HOLD_MS) {
    if (s_usb_ready)
      hid_hal_mouse_click(MOUSE_BUTTON_RIGHT);
    s_right_click_sent = true;
    ui_feedback(UI_FB_SELECT);
  }

  if (!ok && s_ok_last) {
    if (!s_right_click_sent && !s_ok_chord && s_usb_ready) {
      hid_hal_mouse_click(MOUSE_BUTTON_LEFT);
      ui_feedback(UI_FB_SELECT);
    }
    s_ok_down_ms = 0;
  }

  if (s_jiggle || ok) {
    s_ok_last = ok;
    return;
  }

  int8_t dx = 0;
  int8_t dy = 0;
  if (up)
    dy -= MOVE_STEP;
  if (down)
    dy += MOVE_STEP;
  if (left)
    dx -= MOVE_STEP;
  if (right)
    dx += MOVE_STEP;
  bool moved = dx != 0 || dy != 0;
  if (moved) {
    s_cur_x += dx;
    s_cur_y += dy;
    update_cursor();
    if (s_usb_ready)
      hid_hal_mouse_move(dx, dy);
  }
  s_ok_last = ok;
}

void ui_usb_mouse_stop(void) {
  if (s_move_timer != NULL) {
    lv_timer_delete(s_move_timer);
    s_move_timer = NULL;
  }
  if (s_jiggle_timer != NULL) {
    lv_timer_delete(s_jiggle_timer);
    s_jiggle_timer = NULL;
  }
  if (usb_mux_is_native() != s_prev_mux_native)
    (void)usb_mux_set_native(s_prev_mux_native);
  s_usb_ready = false;
}

void ui_usb_mouse_open(void) {
  if (s_jiggle_timer != NULL) {
    lv_timer_delete(s_jiggle_timer);
    s_jiggle_timer = NULL;
  }
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_cursor = NULL;
  s_chip = NULL;
  s_chip_lbl = NULL;
  s_status_dot = NULL;
  s_status_lbl = NULL;
  s_cur_x = CUR_START_X;
  s_cur_y = CUR_START_Y;
  s_jig_ang = 0;
  s_jiggle = false;
  s_ok_last = false;
  s_ok_down_ms = 0;
  s_last_scroll_ms = 0;
  s_right_click_sent = false;
  s_ok_chord = false;
  s_jiggle_chord_latched = false;

  s_prev_mux_native = usb_mux_is_native();
  esp_err_t usb_err = usb_mux_set_native(true);
  if (usb_err == ESP_OK) {
    esp_err_t hid_err = bad_usb_init();
    s_usb_init_ok = hid_err == ESP_OK || hid_err == ESP_ERR_INVALID_STATE;
  } else {
    s_usb_init_ok = false;
  }
  s_usb_ready = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_status();
  build_trackpad();
  build_rail();
  build_click(MX, "L CLICK", true);
  build_click(MX + CLICK_W + ROW_GAP, "R CLICK", false);

  ui_chrome_footer(s_screen, FOOTER_TXT);

  s_jiggle_timer = lv_timer_create(jiggle_tick_cb, JIGGLE_TICK_MS, NULL);
  if (s_move_timer == NULL)
    s_move_timer = lv_timer_create(move_tick_cb, MOVE_TICK_MS, NULL);
  ui_input_set_screen_handler(usb_mouse_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

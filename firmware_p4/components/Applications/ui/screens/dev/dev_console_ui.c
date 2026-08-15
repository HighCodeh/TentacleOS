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

#include "dev_console_ui.h"

#include <string.h>

#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "terminal_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define THUMB_TICK_MS   50
#define STREAM_TIMER_MS 350

#define TITLE       "CONSOLE"
#define ICON        "/assets/icons/monitoring.bin"
#define DRAG_ICON   "/assets/icons/drag_indicator.bin"
#define FOOTER_HINT "UP/DOWN scroll   BACK exit"

#define BODY_X          6
#define BODY_TOP_PAD    4
#define BODY_BOTTOM_PAD 4
#define RIGHT_GUTTER    16
#define BODY_Y          (UI_CHROME_HEADER_H + BODY_TOP_PAD)
#define TERM_W          (LCD_H_RES - BODY_X - RIGHT_GUTTER)
#define TERM_H          (LCD_V_RES - BODY_Y - UI_CHROME_FOOTER_H - BODY_BOTTOM_PAD)

#define SCROLL_TRACK_X   227
#define SCROLL_TRACK_Y   54
#define SCROLL_TRACK_LEN 232
#define SCROLL_LINE_W    3
#define SCROLL_DASH      4
#define SCROLL_THUMB_X   223
#define SCROLL_THUMB_H   45

#define LINE_STEP   24
#define FOLLOW_SLOP 2

#define LOG_SOFT_CAP 1400
#define LOG_KEEP     1000
#define SEED_LINES   6

static const char *const MOCK_LINES[] = {
    "[boot] st7789 init ok",
    "[lvgl] flush 240x320",
    "[i2s] audio ready",
    "[sd] card mounted",
    "[ble] c5 link down",
    "[wifi] c5 offline",
    "[pmu] batt 84% 4.02V",
    "[sys] heap 212KB free",
    "[ui] screen=developer",
    "[usb] cdc idle",
};
#define MOCK_LINE_COUNT ((int)(sizeof(MOCK_LINES) / sizeof(MOCK_LINES[0])))

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_term = NULL;
static lv_obj_t *s_thumb = NULL;
static lv_timer_t *s_thumb_timer = NULL;
static lv_timer_t *s_stream = NULL;

static int s_line_idx = 0;
static bool s_follow = true;

static char s_trim_buf[LOG_KEEP + 1];

static void update_thumb(void) {
  if (s_term == NULL || s_thumb == NULL)
    return;

  lv_obj_update_layout(s_term);

  int32_t thumb_h = lv_obj_get_height(s_thumb);
  if (thumb_h <= 0)
    thumb_h = SCROLL_THUMB_H;

  int32_t travel = SCROLL_TRACK_LEN - thumb_h;
  if (travel < 0)
    travel = 0;

  int32_t scroll_y = lv_obj_get_scroll_y(s_term);
  int32_t scroll_bottom = lv_obj_get_scroll_bottom(s_term);
  int32_t max_scroll = scroll_y + scroll_bottom;

  int32_t pos = SCROLL_TRACK_Y;
  if (max_scroll > 0) {
    if (scroll_y < 0)
      scroll_y = 0;
    if (scroll_y > max_scroll)
      scroll_y = max_scroll;
    pos = SCROLL_TRACK_Y + (scroll_y * travel) / max_scroll;
  }

  if (lv_obj_get_y(s_thumb) != pos)
    lv_obj_set_y(s_thumb, pos);
}

static void push_line(void) {
  if (s_term == NULL)
    return;

  lv_textarea_add_text(s_term, MOCK_LINES[s_line_idx]);
  lv_textarea_add_text(s_term, "\n");

  s_line_idx++;
  if (s_line_idx >= MOCK_LINE_COUNT)
    s_line_idx = 0;

  const char *txt = lv_textarea_get_text(s_term);
  size_t len = (txt != NULL) ? strlen(txt) : 0;
  if (len > LOG_SOFT_CAP) {
    const char *tail = txt + (len - LOG_KEEP);
    const char *nl = strchr(tail, '\n');
    if (nl != NULL)
      tail = nl + 1;
    size_t keep = strlen(tail);
    if (keep > LOG_KEEP)
      keep = LOG_KEEP;
    memcpy(s_trim_buf, tail, keep);
    s_trim_buf[keep] = '\0';
    lv_textarea_set_text(s_term, s_trim_buf);
  }

  lv_obj_update_layout(s_term);
  int32_t bottom = lv_obj_get_scroll_y(s_term) + lv_obj_get_scroll_bottom(s_term);
  lv_obj_scroll_to_y(s_term, bottom, LV_ANIM_OFF);

  update_thumb();
}

static void stream_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_stream = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;
  if (!s_follow)
    return;
  push_line();
}

static void thumb_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_thumb_timer = NULL;
    return;
  }
  update_thumb();
}

static void dev_console_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_UP:
      if (nav) {
        lv_obj_scroll_by(s_term, 0, LINE_STEP, LV_ANIM_ON);
        s_follow = false;
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        int32_t scroll_bottom = lv_obj_get_scroll_bottom(s_term);
        lv_obj_scroll_by(s_term, 0, -LINE_STEP, LV_ANIM_ON);
        if (scroll_bottom <= LINE_STEP + FOLLOW_SLOP)
          s_follow = true;
      }
      break;
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_DEV_MENU);
      break;
    default:
      break;
  }
}

static void build_screen(void) {
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, TITLE, ICON);
  ui_chrome_footer(s_screen, FOOTER_HINT);

  s_term = terminal_ui_create(s_screen, TERM_W, TERM_H, LV_ALIGN_TOP_LEFT, BODY_X, BODY_Y);
  lv_obj_set_scrollbar_mode(s_term, LV_SCROLLBAR_MODE_OFF);
  lv_textarea_set_cursor_click_pos(s_term, false);
  lv_obj_set_style_anim_duration(s_term, 0, LV_PART_CURSOR);
  lv_obj_set_style_opa(s_term, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(s_term, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_border_width(s_term, 0, LV_PART_CURSOR);

  static lv_point_precise_t track_pts[2] = {{0, 0}, {0, SCROLL_TRACK_LEN}};
  lv_obj_t *track = lv_line_create(s_screen);
  lv_line_set_points(track, track_pts, 2);
  lv_obj_set_pos(track, SCROLL_TRACK_X, SCROLL_TRACK_Y);
  lv_obj_set_style_line_width(track, SCROLL_LINE_W, 0);
  lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_dash_width(track, SCROLL_DASH, 0);
  lv_obj_set_style_line_dash_gap(track, SCROLL_DASH, 0);

  s_thumb = lv_image_create(s_screen);
  lv_image_dsc_t *thumb_dsc = assets_get(DRAG_ICON);
  if (thumb_dsc != NULL)
    lv_image_set_src(s_thumb, thumb_dsc);
  lv_obj_set_pos(s_thumb, SCROLL_THUMB_X, SCROLL_TRACK_Y);
  lv_obj_move_foreground(s_thumb);

  for (int i = 0; i < SEED_LINES; i++)
    push_line();

  update_thumb();
  ui_input_set_screen_handler(dev_console_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

void ui_dev_console_open(void) {
  s_screen = NULL;
  s_term = NULL;
  s_thumb = NULL;
  s_thumb_timer = NULL;
  s_stream = NULL;
  s_line_idx = 0;
  s_follow = true;
  s_trim_buf[0] = '\0';

  build_screen();

  s_thumb_timer = lv_timer_create(thumb_tick_cb, THUMB_TICK_MS, NULL);
  s_stream = lv_timer_create(stream_cb, STREAM_TIMER_MS, NULL);
}

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

#include "c5_console_ui.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "terminal_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define THUMB_TICK_MS 50
#define DRAIN_TICK_MS 120

#define TITLE       "C5 CONSOLE"
#define ICON        "/assets/icons/monitoring.bin"
#define DRAG_ICON   "/assets/icons/drag_indicator.bin"
#define FOOTER_HINT "UP/DOWN scroll   BACK exit"

#define BODY_X          6
#define BODY_TOP_PAD    4
#define BODY_BOTTOM_PAD 4
#define RIGHT_GUTTER    16
#define BODY_Y          (UI_CHROME_HEADER_H + BODY_TOP_PAD)
#define TERM_W          (ui_screen_w() - BODY_X - RIGHT_GUTTER)
#define TERM_H          (ui_screen_h() - BODY_Y - UI_CHROME_FOOTER_H - BODY_BOTTOM_PAD)

#define SCROLL_TRACK_X   (ui_screen_w() - 13)
#define SCROLL_TRACK_Y   54
#define SCROLL_TRACK_LEN (ui_screen_h() - SCROLL_TRACK_Y - 34)
#define SCROLL_LINE_W    3
#define SCROLL_DASH      4
#define SCROLL_THUMB_X   (ui_screen_w() - 17)
#define SCROLL_THUMB_H   45

#define LINE_STEP   24
#define FOLLOW_SLOP 2

#define LOG_SOFT_CAP 1400
#define LOG_KEEP     1000

#define RING_SIZE   4096
#define DRAIN_CHUNK 512

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_term = NULL;
static lv_obj_t *s_thumb = NULL;
static lv_timer_t *s_thumb_timer = NULL;
static lv_timer_t *s_drain = NULL;
static bool s_follow = true;
static char s_trim_buf[LOG_KEEP + 1];

static char s_hist[RING_SIZE];
static volatile uint32_t s_hist_head = 0;
static volatile uint64_t s_hist_total = 0;
static portMUX_TYPE s_hist_mux = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_read_cursor = 0;

static void hist_write(const char *data, uint16_t len) {
  portENTER_CRITICAL(&s_hist_mux);
  for (uint16_t i = 0; i < len; i++) {
    s_hist[s_hist_head] = data[i];
    s_hist_head = (s_hist_head + 1) % RING_SIZE;
    s_hist_total++;
  }
  portEXIT_CRITICAL(&s_hist_mux);
}

static uint16_t hist_read_since(char *out, uint16_t cap, uint64_t *cursor) {
  portENTER_CRITICAL(&s_hist_mux);
  uint64_t total = s_hist_total;
  if (*cursor > total)
    *cursor = total;
  uint64_t behind = total - *cursor;
  if (behind > RING_SIZE) {
    *cursor = total - RING_SIZE;
    behind = RING_SIZE;
  }
  uint32_t want = (behind > cap) ? cap : (uint32_t)behind;
  uint32_t start = (uint32_t)((s_hist_head + RING_SIZE - (uint32_t)behind) % RING_SIZE);
  uint16_t n = 0;
  for (uint32_t i = 0; i < want; i++)
    out[n++] = s_hist[(start + i) % RING_SIZE];
  *cursor += n;
  portEXIT_CRITICAL(&s_hist_mux);
  return n;
}

static void on_c5_log(spi_id_t id, const uint8_t *payload, uint8_t len) {
  (void)id;
  if (payload == NULL || len < 2)
    return;
  hist_write((const char *)(payload + 1), (uint16_t)(len - 1));
  if (payload[len - 1] != '\n')
    hist_write("\n", 1);
}

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

static void append_text(const char *txt) {
  if (s_term == NULL || txt == NULL || txt[0] == '\0')
    return;
  lv_textarea_add_text(s_term, txt);

  const char *cur = lv_textarea_get_text(s_term);
  size_t len = (cur != NULL) ? strlen(cur) : 0;
  if (len > LOG_SOFT_CAP) {
    const char *tail = cur + (len - LOG_KEEP);
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

  if (s_follow) {
    lv_obj_update_layout(s_term);
    int32_t bottom = lv_obj_get_scroll_y(s_term) + lv_obj_get_scroll_bottom(s_term);
    lv_obj_scroll_to_y(s_term, bottom, LV_ANIM_OFF);
  }
  update_thumb();
}

static void drain_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_drain = NULL;
    return;
  }
  char chunk[DRAIN_CHUNK + 1];
  uint16_t n = hist_read_since(chunk, DRAIN_CHUNK, &s_read_cursor);
  if (n == 0)
    return;
  chunk[n] = '\0';
  append_text(chunk);
}

static void thumb_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_thumb_timer = NULL;
    return;
  }
  update_thumb();
}

static void c5_console_input(const input_event_t *ev, void *ctx) {
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
      if (press)
        ui_switch_screen(SCREEN_C5_STATUS);
      break;
    default:
      break;
  }
}

static void on_delete(lv_event_t *e) {
  (void)e;
  if (s_drain) {
    lv_timer_delete(s_drain);
    s_drain = NULL;
  }
  if (s_thumb_timer) {
    lv_timer_delete(s_thumb_timer);
    s_thumb_timer = NULL;
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

  static lv_point_precise_t track_pts[2];
  track_pts[0].x = 0;
  track_pts[0].y = 0;
  track_pts[1].x = 0;
  track_pts[1].y = SCROLL_TRACK_LEN;
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

  lv_textarea_set_placeholder_text(s_term, "Waiting for C5 log...");
  update_thumb();

  lv_obj_add_event_cb(s_screen, on_delete, LV_EVENT_DELETE, NULL);
  ui_input_set_screen_handler(c5_console_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

void ui_c5_console_open(void) {
  s_screen = NULL;
  s_term = NULL;
  s_thumb = NULL;
  s_thumb_timer = NULL;
  s_drain = NULL;
  s_follow = true;
  s_trim_buf[0] = '\0';
  s_read_cursor = 0;

  spi_bridge_register_stream_cb(SPI_ID_SYSTEM_LOG, on_c5_log);
  build_screen();

  s_thumb_timer = lv_timer_create(thumb_tick_cb, THUMB_TICK_MS, NULL);
  s_drain = lv_timer_create(drain_cb, DRAIN_TICK_MS, NULL);
}

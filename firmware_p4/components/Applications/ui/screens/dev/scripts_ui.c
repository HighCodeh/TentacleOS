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

#include "scripts_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "text_viewer_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

static const char *TAG = "SCRIPTS_UI";

#define FADE_MS         200
#define PENDING_POLL_MS 50

#define TERM_GREEN       0x00E676
#define TERM_DIM_GREEN   0x1F7A52
#define DARK_PANEL_COLOR 0x05090A
#define DANGER_COLOR     0xFF5252

#define HEADER_TITLE "SCRIPTS"
#define HEADER_ICON  "/assets/icons/description.bin"
#define ERROR_ICON   "/assets/icons/error.bin"

#define BROWSER_FOOTER "UP/DOWN pick   OK open   BACK back"
#define VIEWER_FOOTER  "OK run   BACK back"
#define RUN_FOOTER     "BACK = Abort"
#define DONE_FOOTER    "RIGHT = Run again   BACK = Back"

#define LIST_X         6
#define LIST_Y         46
#define LIST_GUTTER    16
#define LIST_W         (ui_screen_w() - LIST_X - LIST_GUTTER)
#define LIST_BODY_H    (ui_screen_h() - LIST_Y - UI_CHROME_FOOTER_H - 4)
#define ROW_H          26
#define ROW_GAP        4
#define ROW_RADIUS     8
#define ROW_PAD_HOR    8
#define ROW_COL_GAP    6
#define ROW_GLOW_W     14
#define CHEVRON_TEXT   LV_SYMBOL_RIGHT
#define BADGE_RADIUS   4
#define BADGE_PAD_HOR  4
#define BADGE_PAD_VER  1
#define BADGE_TINT_OPA LV_OPA_20

#define SCROLL_TRACK_X          (ui_screen_w() - 13)
#define SCROLL_TRACK_Y          54
#define SCROLL_TRACK_LEN        (ui_screen_h() - SCROLL_TRACK_Y - 34)
#define SCROLL_TRACK_WIDTH      3
#define SCROLL_DASH             4
#define SCROLL_THUMB_X          (ui_screen_w() - 17)
#define SCROLL_THUMB_FALLBACK_H 45
#define SCROLL_THUMB_SRC        "/assets/icons/drag_indicator.bin"

#define VIEWER_SCROLL_STEP 28
#define VIEWER_SRC_LEN     320

#define EMPTY_CARD_W 204
#define EMPTY_CARD_H 100
#define EMPTY_GLOW_W 18

#define TERM_W        216
#define TERM_H        150
#define TERM_TOP_Y    52
#define TERM_PAD      8
#define TERM_BORDER   2
#define TERM_HEADER_Y 0
#define TERM_BODY_Y   18
#define TERM_PROMPT   "tentacle@p4:~$ js"
#define TERM_BUF_LEN  320

#define PCT_TEXT             "Running"
#define PCT_Y                210
#define PROGRESS_W           214
#define PROGRESS_H           8
#define PROGRESS_Y           228
#define PROGRESS_RADIUS      4
#define PROGRESS_TRACK_COLOR 0x10211A

#define RESULT_Y           248
#define TYPE_TICK_MS       42
#define CURSOR_BLINK_TICKS 9
#define DONE_DELAY_MS      420

#define STREAM_MAX     6
#define STREAM_HDR_LEN 48
#define PERM_MSG_LEN   96

#define SCRIPT_CODE_MAX 3
#define SCRIPT_CAP_MAX  3

typedef enum {
  CAP_NFC = 0,
  CAP_USB,
  CAP_C5,
  CAP_LED,
  CAP_IR,
  CAP_COUNT,
} cap_t;

static const struct {
  const char *badge;
  const char *control;
  bool available;
  bool danger;
} CAPS[CAP_COUNT] = {
    {"NFC", "NFC", true, false},
    {"USB", "USB HID", true, true},
    {"C5", "Wi-Fi", false, true},
    {"LED", "LED", true, false},
    {"IR", "IR", true, false},
};

typedef struct {
  const char *name;
  const char *desc;
  cap_t caps[SCRIPT_CAP_MAX];
  int cap_count;
  const char *code[SCRIPT_CODE_MAX];
  int code_count;
  bool error_outcome;
  const char *result;
  const char *error;
} script_t;

static const script_t SCRIPTS[] = {
    {"badge.js",
     "Reads an NFC card and types the UID",
     {CAP_NFC, CAP_USB},
     2,
     {"let c = nfc.read()", "usb.type(c.uid)"},
     2,
     false,
     "value: 04A23B9C",
     NULL},
    {"wifi_probe.js",
     "Probe nearby Wi-Fi APs",
     {CAP_C5},
     1,
     {"let aps = wifi.scan()", "print(aps.length)"},
     2,
     false,
     "value: 6 APs",
     NULL},
    {"blink.js",
     "Blink the status LED",
     {CAP_LED},
     1,
     {"led.on()", "delay(200)", "led.off()"},
     3,
     false,
     "value: ok",
     NULL},
    {"tvoff.js",
     "Turn off any TV nearby",
     {CAP_IR},
     1,
     {"ir.send('NEC', 0x20DF10EF)"},
     1,
     false,
     "value: sent",
     NULL},
    {"uid_dump.js",
     "Dump card memory to console",
     {CAP_NFC},
     1,
     {"let c = nfc.read()", "dump(c.blocks)"},
     2,
     true,
     NULL,
     "nfc is not defined  line 3"},
    {"duck.js",
     "Type a scripted HID payload",
     {CAP_USB},
     1,
     {"usb.press('GUI r')", "usb.type('notepad')"},
     2,
     false,
     "value: injected",
     NULL},
    {"ir_learn.js",
     "Learn one IR remote button",
     {CAP_IR},
     1,
     {"let s = ir.recv()", "save(s)"},
     2,
     false,
     "value: captured",
     NULL},
    {"rainbow.js",
     "Cycle the LED through hues",
     {CAP_LED},
     1,
     {"for (h = 0; h < 360; h++)", "led.hue(h)"},
     2,
     false,
     "value: ok",
     NULL},
    {"deauth.js",
     "Flood deauth on a Wi-Fi target",
     {CAP_C5, CAP_USB},
     2,
     {"wifi.deauth(bssid)", "usb.log('sent')"},
     2,
     false,
     "value: n/a",
     NULL},
    {"clone.js",
     "Clone a card UID over HID",
     {CAP_NFC, CAP_USB},
     2,
     {"let c = nfc.read()", "usb.type(c.uid)"},
     2,
     false,
     "value: cloned",
     NULL},
};
#define SCRIPT_COUNT ((int)(sizeof(SCRIPTS) / sizeof(SCRIPTS[0])))

typedef enum {
  VIEW_BROWSER = 0,
  VIEW_VIEWER,
  VIEW_RUNNING,
} view_t;

typedef enum {
  RUN_STAGE_STREAMING = 0,
  RUN_STAGE_DONE,
} run_stage_t;

static lv_obj_t *s_screen = NULL;
static view_t s_view = VIEW_BROWSER;
static int s_sel = 0;
static bool s_pending_run = false;

static lv_timer_t *s_pending_timer = NULL;
static lv_timer_t *s_type_timer = NULL;
static lv_timer_t *s_stage_timer = NULL;

static lv_obj_t *s_rows[SCRIPT_COUNT];
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_thumb = NULL;
static text_viewer_t s_tv;

static lv_obj_t *s_footer = NULL;
static lv_obj_t *s_term_lbl = NULL;
static lv_obj_t *s_progress = NULL;
static lv_obj_t *s_pct_lbl = NULL;

static run_stage_t s_run_stage = RUN_STAGE_STREAMING;
static const char *s_stream[STREAM_MAX];
static int s_stream_count = 0;
static char s_run_hdr[STREAM_HDR_LEN];
static char s_term_buf[TERM_BUF_LEN];
static int s_type_line = 0;
static int s_type_col = 0;
static int s_typed_chars = 0;
static int s_total_chars = 0;
static int s_cursor_ticks = 0;
static bool s_cursor_on = true;

static void scripts_pending_cb(lv_timer_t *t);
static void scripts_input(const input_event_t *ev, void *ctx);
static void build_screen(void);
static void type_tick_cb(lv_timer_t *t);
static void stage_advance_cb(lv_timer_t *t);
static void update_scrollbar(void);
static void build_viewer(void);

static void stop_type_timer(void) {
  if (s_type_timer != NULL) {
    lv_timer_delete(s_type_timer);
    s_type_timer = NULL;
  }
}

static void stop_stage_timer(void) {
  if (s_stage_timer != NULL) {
    lv_timer_delete(s_stage_timer);
    s_stage_timer = NULL;
  }
}

static void opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fade_in(lv_obj_t *obj, uint32_t duration_ms) {
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, duration_ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static bool script_has_unavailable(const script_t *s) {
  for (int i = 0; i < s->cap_count; i++)
    if (!CAPS[s->caps[i]].available)
      return true;
  return false;
}

static bool script_needs_permission(const script_t *s) {
  for (int i = 0; i < s->cap_count; i++)
    if (CAPS[s->caps[i]].danger)
      return true;
  return false;
}

static void build_badge(lv_obj_t *parent, cap_t c) {
  bool avail = CAPS[c].available;
  lv_color_t color = avail ? current_theme.border_accent : current_theme.text_secondary;

  lv_obj_t *badge = lv_obj_create(parent);
  lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(badge, BADGE_RADIUS, 0);
  lv_obj_set_style_pad_hor(badge, BADGE_PAD_HOR, 0);
  lv_obj_set_style_pad_ver(badge, BADGE_PAD_VER, 0);
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_border_color(badge, color, 0);
  lv_obj_set_style_bg_color(badge, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(badge, avail ? BADGE_TINT_OPA : LV_OPA_TRANSP, 0);

  lv_obj_t *lbl = lv_label_create(badge);
  lv_label_set_text(lbl, CAPS[c].badge);
  lv_obj_set_style_text_color(lbl, color, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(lbl);
}

static void style_row(lv_obj_t *row, bool selected) {
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  if (selected) {
    lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
    lv_obj_set_style_border_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, ROW_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(row, -2, 0);
  } else {
    lv_obj_set_style_bg_color(row, current_theme.bg_primary, 0);
    lv_obj_set_style_border_color(row, current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
  }
}

static void update_scrollbar(void) {
  if (s_thumb == NULL)
    return;
  int thumb_h = (int)lv_obj_get_height(s_thumb);
  if (thumb_h <= 0)
    thumb_h = SCROLL_THUMB_FALLBACK_H;
  int travel = SCROLL_TRACK_LEN - thumb_h;
  if (travel < 0)
    travel = 0;
  int y = SCROLL_TRACK_Y;
  if (SCRIPT_COUNT > 1)
    y = SCROLL_TRACK_Y + (s_sel * travel) / (SCRIPT_COUNT - 1);
  lv_obj_set_y(s_thumb, y);
}

static void apply_sel(int idx) {
  for (int i = 0; i < SCRIPT_COUNT; i++)
    if (s_rows[i] != NULL)
      style_row(s_rows[i], i == idx);
  if (s_list != NULL && s_rows[idx] != NULL)
    lv_obj_scroll_to_view(s_rows[idx], LV_ANIM_ON);
  update_scrollbar();
}

static void build_empty(void) {
  ui_chrome_footer(s_screen, BROWSER_FOOTER);

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, EMPTY_CARD_W, EMPTY_CARD_H);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, EMPTY_GLOW_W, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_obj_t *t1 = lv_label_create(card);
  lv_label_set_text(t1, "No scripts");
  lv_obj_set_style_text_font(t1, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t1, current_theme.text_main, 0);

  lv_obj_t *t2 = lv_label_create(card);
  lv_label_set_text(t2, "Copy .js to /apps/scripts");
  lv_obj_set_style_text_font(t2, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(t2, current_theme.text_secondary, 0);

  fade_in(card, FADE_MS);
}

static void build_browser(void) {
  ui_chrome_header(s_screen, HEADER_TITLE, HEADER_ICON);

  if (SCRIPT_COUNT == 0) {
    build_empty();
    return;
  }

  ui_chrome_footer(s_screen, BROWSER_FOOTER);

  if (s_sel < 0)
    s_sel = 0;
  if (s_sel >= SCRIPT_COUNT)
    s_sel = SCRIPT_COUNT - 1;

  lv_obj_t *list = lv_obj_create(s_screen);
  lv_obj_set_size(list, LIST_W, LIST_BODY_H);
  lv_obj_set_pos(list, LIST_X, LIST_Y);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, ROW_GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  s_list = list;

  for (int i = 0; i < SCRIPT_COUNT; i++) {
    const script_t *s = &SCRIPTS[i];

    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_obj_set_style_radius(row, ROW_RADIUS, 0);
    lv_obj_set_style_pad_hor(row, ROW_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ROW_COL_GAP, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, s->name);
    lv_obj_set_style_text_color(name, current_theme.text_main, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(name, 1);

    for (int c = 0; c < s->cap_count; c++)
      build_badge(row, s->caps[c]);

    lv_obj_t *chev = lv_label_create(row);
    lv_label_set_text(chev, CHEVRON_TEXT);
    lv_obj_set_style_text_color(chev, current_theme.text_secondary, 0);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_12, 0);

    s_rows[i] = row;
  }

  static lv_point_precise_t scroll_pts[2];
  scroll_pts[0].x = 0;
  scroll_pts[0].y = 0;
  scroll_pts[1].x = 0;
  scroll_pts[1].y = SCROLL_TRACK_LEN;
  lv_obj_t *track = lv_line_create(s_screen);
  lv_line_set_points(track, scroll_pts, 2);
  lv_obj_set_pos(track, SCROLL_TRACK_X, SCROLL_TRACK_Y);
  lv_obj_set_style_line_width(track, SCROLL_TRACK_WIDTH, 0);
  lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_dash_width(track, SCROLL_DASH, 0);
  lv_obj_set_style_line_dash_gap(track, SCROLL_DASH, 0);

  s_thumb = lv_image_create(s_screen);
  lv_image_dsc_t *thumb_dsc = assets_get(SCROLL_THUMB_SRC);
  if (thumb_dsc != NULL)
    lv_image_set_src(s_thumb, thumb_dsc);
  lv_obj_set_pos(s_thumb, SCROLL_THUMB_X, SCROLL_TRACK_Y);
  lv_obj_move_foreground(s_thumb);

  lv_obj_update_layout(list);
  apply_sel(s_sel);

  fade_in(list, FADE_MS);
}

static void build_terminal(void) {
  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, TERM_W, TERM_H);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, TERM_TOP_Y);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(DARK_PANEL_COLOR), 0);
  lv_obj_set_style_border_width(panel, TERM_BORDER, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_70, 0);
  lv_obj_set_style_shadow_color(panel, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_shadow_width(panel, 12, 0);
  lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(panel, TERM_PAD, 0);

  lv_obj_t *prompt = lv_label_create(panel);
  lv_label_set_text(prompt, TERM_PROMPT);
  lv_obj_set_style_text_color(prompt, lv_color_hex(TERM_DIM_GREEN), 0);
  lv_obj_set_style_text_font(prompt, &lv_font_montserrat_12, 0);
  lv_obj_align(prompt, LV_ALIGN_TOP_LEFT, 0, TERM_HEADER_Y);

  s_term_lbl = lv_label_create(panel);
  lv_label_set_text(s_term_lbl, "");
  lv_obj_set_width(s_term_lbl, TERM_W - TERM_PAD * 2);
  lv_label_set_long_mode(s_term_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(s_term_lbl, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_term_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_term_lbl, LV_ALIGN_TOP_LEFT, 0, TERM_BODY_Y);

  s_pct_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_pct_lbl, PCT_TEXT "   0%");
  lv_obj_set_style_text_color(s_pct_lbl, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_pct_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(
      s_pct_lbl, LV_ALIGN_TOP_MID, 0, LV_MIN(PCT_Y, ui_screen_h() - UI_CHROME_FOOTER_H - 56));

  s_progress = lv_bar_create(s_screen);
  lv_obj_set_size(s_progress, PROGRESS_W, PROGRESS_H);
  lv_obj_align(s_progress,
               LV_ALIGN_TOP_MID,
               0,
               LV_MIN(PCT_Y, ui_screen_h() - UI_CHROME_FOOTER_H - 56) + (PROGRESS_Y - PCT_Y));
  lv_bar_set_range(s_progress, 0, 100);
  lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_progress, lv_color_hex(PROGRESS_TRACK_COLOR), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_progress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_progress, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_progress, lv_color_hex(TERM_DIM_GREEN), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_progress, lv_color_hex(TERM_DIM_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(s_progress, lv_color_hex(TERM_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(s_progress, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_progress, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_progress, PROGRESS_RADIUS, LV_PART_MAIN);
  lv_obj_set_style_radius(s_progress, PROGRESS_RADIUS, LV_PART_INDICATOR);
}

static void render_terminal(void) {
  s_term_buf[0] = '\0';
  int pos = 0;
  for (int i = 0; i < s_type_line && i < s_stream_count; i++) {
    pos += snprintf(s_term_buf + pos, TERM_BUF_LEN - pos, "%s\n", s_stream[i]);
    if (pos >= TERM_BUF_LEN)
      pos = TERM_BUF_LEN - 1;
  }
  if (s_type_line < s_stream_count) {
    pos +=
        snprintf(s_term_buf + pos, TERM_BUF_LEN - pos, "%.*s", s_type_col, s_stream[s_type_line]);
    if (pos >= TERM_BUF_LEN)
      pos = TERM_BUF_LEN - 1;
  }
  if (s_cursor_on && pos < TERM_BUF_LEN - 2) {
    s_term_buf[pos++] = '_';
    s_term_buf[pos] = '\0';
  }
  if (s_term_lbl)
    lv_label_set_text(s_term_lbl, s_term_buf);
}

static void show_done(void) {
  const script_t *s = &SCRIPTS[s_sel];

  lv_obj_t *result = lv_label_create(s_screen);
  if (s->error_outcome) {
    char buf[80];
    snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE "  Error: %s", s->error);
    lv_label_set_text(result, buf);
    lv_obj_set_style_text_color(result, lv_color_hex(DANGER_COLOR), 0);
    notify(NOTIFY_WARNING, "Script error");
    ui_feedback(UI_FB_SELECT);
    ESP_LOGI(TAG, "mock script error: %s", s->name);
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  %s", s->result);
    lv_label_set_text(result, buf);
    lv_obj_set_style_text_color(result, lv_color_hex(UI_COL_SUCCESS), 0);
    ui_feedback(UI_FB_WRITE);
    ESP_LOGI(TAG, "mock script done: %s", s->name);
  }
  lv_obj_set_width(result, TERM_W);
  lv_label_set_long_mode(result, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(result, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(result, &lv_font_montserrat_14, 0);
  lv_obj_align(result,
               LV_ALIGN_TOP_MID,
               0,
               LV_MIN(PCT_Y, ui_screen_h() - UI_CHROME_FOOTER_H - 56) + (RESULT_Y - PCT_Y));
  fade_in(result, FADE_MS);

  if (s_footer != NULL)
    ui_chrome_footer_set_text(s_footer, DONE_FOOTER);
}

static void stage_advance_cb(lv_timer_t *t) {
  (void)t;
  s_stage_timer = NULL;
  if (lv_screen_active() != s_screen || s_view != VIEW_RUNNING)
    return;
  if (s_run_stage == RUN_STAGE_DONE)
    show_done();
}

static void type_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen || s_view != VIEW_RUNNING) {
    lv_timer_delete(t);
    s_type_timer = NULL;
    return;
  }

  s_cursor_ticks++;
  if (s_cursor_ticks >= CURSOR_BLINK_TICKS) {
    s_cursor_ticks = 0;
    s_cursor_on = !s_cursor_on;
  }

  if (s_type_line < s_stream_count) {
    int line_len = (int)strlen(s_stream[s_type_line]);
    if (s_type_col < line_len) {
      s_type_col++;
      s_typed_chars++;
    } else {
      s_type_line++;
      s_type_col = 0;
    }
    if (s_total_chars > 0) {
      int pct = s_typed_chars * 100 / s_total_chars;
      if (s_progress)
        lv_bar_set_value(s_progress, pct, LV_ANIM_OFF);
      if (s_pct_lbl) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%s   %d%%", PCT_TEXT, pct);
        lv_label_set_text(s_pct_lbl, buf);
      }
    }
    render_terminal();
  } else {
    render_terminal();
    lv_timer_delete(t);
    s_type_timer = NULL;
    if (s_progress)
      lv_bar_set_value(s_progress, 100, LV_ANIM_ON);
    if (s_pct_lbl)
      lv_label_set_text(s_pct_lbl, PCT_TEXT "   100%");
    s_run_stage = RUN_STAGE_DONE;
    s_stage_timer = lv_timer_create(stage_advance_cb, DONE_DELAY_MS, NULL);
    lv_timer_set_repeat_count(s_stage_timer, 1);
  }
}

static void build_running(void) {
  const script_t *s = &SCRIPTS[s_sel];

  ui_chrome_header(s_screen, HEADER_TITLE, HEADER_ICON);
  s_footer = ui_chrome_footer(s_screen, RUN_FOOTER);

  snprintf(s_run_hdr, sizeof(s_run_hdr), "$ run %s", s->name);
  s_stream_count = 0;
  s_stream[s_stream_count++] = s_run_hdr;
  for (int i = 0; i < s->code_count && s_stream_count < STREAM_MAX; i++)
    s_stream[s_stream_count++] = s->code[i];

  s_run_stage = RUN_STAGE_STREAMING;
  s_type_line = 0;
  s_type_col = 0;
  s_typed_chars = 0;
  s_cursor_ticks = 0;
  s_cursor_on = true;
  s_total_chars = 0;
  for (int i = 0; i < s_stream_count; i++)
    s_total_chars += (int)strlen(s_stream[i]);

  build_terminal();
  render_terminal();
  fade_in(s_term_lbl, FADE_MS);

  s_type_timer = lv_timer_create(type_tick_cb, TYPE_TICK_MS, NULL);
}

static void build_script_source(const script_t *s, char *buf, int n) {
  int pos = snprintf(buf, n, "// %s\n// @desc %s\n// caps:", s->name, s->desc);
  if (pos >= n)
    pos = n - 1;
  for (int i = 0; i < s->cap_count && pos < n - 1; i++) {
    pos += snprintf(buf + pos, n - pos, " %s", CAPS[s->caps[i]].badge);
    if (pos >= n)
      pos = n - 1;
  }
  pos += snprintf(buf + pos, n - pos, "\n\n\"use strict\";\n\n");
  if (pos >= n)
    pos = n - 1;
  for (int i = 0; i < s->code_count && pos < n - 1; i++) {
    pos += snprintf(buf + pos, n - pos, "%s\n", s->code[i]);
    if (pos >= n)
      pos = n - 1;
  }
  snprintf(buf + pos, n - pos, "\nprint(\"%s done\")\n", s->name);
}

static void build_viewer(void) {
  static char src[VIEWER_SRC_LEN];
  const script_t *s = &SCRIPTS[s_sel];
  s_tv = text_viewer_create(s_screen, s->name);
  build_script_source(s, src, VIEWER_SRC_LEN);
  text_viewer_set_text(&s_tv, src);
  ui_chrome_footer(s_screen, VIEWER_FOOTER);
}

static void build_screen(void) {
  stop_type_timer();
  stop_stage_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_list = NULL;
  s_thumb = NULL;
  memset(&s_tv, 0, sizeof(s_tv));
  s_footer = NULL;
  s_term_lbl = NULL;
  s_progress = NULL;
  s_pct_lbl = NULL;
  for (int i = 0; i < SCRIPT_COUNT; i++)
    s_rows[i] = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_view) {
    case VIEW_RUNNING:
      build_running();
      break;
    case VIEW_VIEWER:
      build_viewer();
      break;
    case VIEW_BROWSER:
    default:
      build_browser();
      break;
  }

  if (s_pending_timer == NULL)
    s_pending_timer = lv_timer_create(scripts_pending_cb, PENDING_POLL_MS, NULL);
  ui_input_set_screen_handler(scripts_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void on_perm_confirm(bool confirm) {
  if (confirm)
    s_pending_run = true;
}

static void try_run_selected(void) {
  const script_t *s = &SCRIPTS[s_sel];

  if (script_has_unavailable(s)) {
    msgbox_open_info(ERROR_ICON, "Requires C5 (offline)", s->desc, lv_color_hex(DANGER_COLOR));
    notify(NOTIFY_WARNING, "C5 offline");
    return;
  }

  if (script_needs_permission(s)) {
    char msg[PERM_MSG_LEN];
    int pos = snprintf(msg, sizeof(msg), "Run %s? It can control: ", s->name);
    for (int i = 0; i < s->cap_count && pos < PERM_MSG_LEN - 1; i++)
      pos += snprintf(
          msg + pos, PERM_MSG_LEN - pos, "%s%s", i == 0 ? "" : ", ", CAPS[s->caps[i]].control);
    msgbox_open(LV_SYMBOL_WARNING, msg, "Run", "Cancel", on_perm_confirm);
    return;
  }

  s_pending_run = true;
}

static void scripts_pending_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_pending_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  if (s_pending_run) {
    s_pending_run = false;
    s_view = VIEW_RUNNING;
    build_screen();
  }
}

static void scripts_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_view) {
    case VIEW_BROWSER:
      if (SCRIPT_COUNT == 0) {
        if (ev->button == INPUT_BTN_BACK && press)
          ui_switch_screen(SCREEN_DEV_MENU);
        break;
      }
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_sel < SCRIPT_COUNT - 1) {
            s_sel++;
            apply_sel(s_sel);
          }
          break;
        case INPUT_BTN_UP:
          if (nav && s_sel > 0) {
            s_sel--;
            apply_sel(s_sel);
          }
          break;
        case INPUT_BTN_OK:
          if (press) {
            s_view = VIEW_VIEWER;
            build_screen();
          }
          break;
        case INPUT_BTN_BACK:
          if (press)
            ui_switch_screen(SCREEN_DEV_MENU);
          break;
        default:
          break;
      }
      break;

    case VIEW_VIEWER:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_tv.text_area != NULL)
            lv_obj_scroll_by(s_tv.text_area, 0, -VIEWER_SCROLL_STEP, LV_ANIM_ON);
          break;
        case INPUT_BTN_UP:
          if (nav && s_tv.text_area != NULL)
            lv_obj_scroll_by(s_tv.text_area, 0, VIEWER_SCROLL_STEP, LV_ANIM_ON);
          break;
        case INPUT_BTN_OK:
          if (press)
            try_run_selected();
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_BROWSER;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;

    case VIEW_RUNNING:
      switch (ev->button) {
        case INPUT_BTN_RIGHT:
          if (press && s_run_stage == RUN_STAGE_DONE)
            build_screen();
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_BROWSER;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;
  }
}

void ui_scripts_open(void) {
  s_pending_timer = NULL;
  s_type_timer = NULL;
  s_stage_timer = NULL;
  s_view = VIEW_BROWSER;
  s_sel = 0;
  s_pending_run = false;
  build_screen();
}

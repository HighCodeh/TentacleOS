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

#include "subghz_menu_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "octobit_ui.h"
#include "sigwave_ui.h"
#include "subghz_receiver.h"
#include "subghz_replay.h"
#include "subghz_scope_ui.h"
#include "subghz_spectrum.h"
#include "subghz_storage.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define FADE_MS 200

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2

#define BAR_COUNT      16
#define BAR_W          8
#define BAR_GAP        4
#define BAR_MIN_H      6
#define BAR_MAX_H      200
#define BAR_BASELINE_Y (ui_screen_h() - UI_CHROME_FOOTER_H - 10)
#define FREQ_CYCLE_MS  400

#define CARD_W       200
#define CARD_H       108
#define CARD_CTR_Y   -34
#define CARD_RISE_PX 70
#define CARD_RISE_MS 450

#define INFO_ACT_BOTTOM_Y -30
#define INFO_ACT_W        200
#define INFO_ACT_H        32
#define INFO_ACT_GAP      8

#define HINT_ANALYZER "BACK exit"

#define SGC_LEFT        6
#define SGC_GUTTER      16
#define SGC_TOP_Y       46
#define SGC_LIST_PAD    2
#define SGC_LIST_ROW    8
#define SGC_CARD_H      86
#define SGC_CARD_RADIUS 12
#define SGC_CARD_PAD    10
#define SGC_GLOW_W      14
#define SGC_BARS        8
#define SGC_BARS_H      16
#define SGC_BAR_W       6
#define SGC_BAR_MIN     3
#define SGC_BAR_SPAN    12
#define SGC_TRACK_X     (ui_screen_w() - 13)
#define SGC_TRACK_Y     54
#define SGC_TRACK_LEN   LV_MIN(232, ui_screen_h() - UI_CHROME_FOOTER_H - SGC_TRACK_Y)
#define SGC_THUMB_H     45
#define SGC_THUMB_ICON  "/assets/icons/drag_indicator.bin"

#define COL_RAISE 0x170A28

#define HINT_SAVED "UP/DOWN choose   OK open   BACK exit"
#define HINT_INFO  "UP/DOWN choose   OK do   BACK exit"

#define SEND_ANIM_MS  1500
#define SEND_DONE_MS  750
#define SEND_STATUS_Y 48
#define SEND_FREQ_Y   68
#define SEND_SCOPE_Y  -8

static const struct {
  const char *name;
  const char *icon;
  bool capture;
} ITEMS[] = {
    {"Read", "/assets/icons/sensors.bin", true},
    {"Read RAW", "/assets/icons/raw_on.bin", true},
    {"Frequency Analyzer", "/assets/icons/graphic_eq.bin", false},
    {"Brute Force", "/assets/icons/bolt.bin", true},
    {"Saved", "/assets/icons/folder.bin", false},
    {"Send", "/assets/icons/sensors.bin", false},
    {"Radio Config", "/assets/icons/tune.bin", false},
};
#define ITEM_COUNT ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

#define IDX_ANALYZER 2
#define IDX_BRUTE    3
#define IDX_SAVED    4
#define IDX_SEND     5
#define IDX_CONFIG   6

#define SAVED_MAX 64
static subghz_storage_entry_t s_saved[SAVED_MAX];
static int s_saved_count = 0;

static void fmt_mhz(uint32_t hz, char *out, size_t n) {
  if (hz == 0) {
    snprintf(out, n, "-- MHz");
    return;
  }
  snprintf(out,
           n,
           "%lu.%02lu MHz",
           (unsigned long)(hz / 1000000UL),
           (unsigned long)((hz % 1000000UL) / 10000UL));
}

static void load_saved(void) {
  int n = subghz_storage_list(s_saved, SAVED_MAX);
  s_saved_count = (n < 0) ? 0 : n;
}

static const struct {
  const char *icon;
  const char *label;
} INFO_ACTIONS[] = {
    {LV_SYMBOL_UPLOAD, "Send"},
    {LV_SYMBOL_TRASH, "Delete"},
};
#define INFO_ACTION_COUNT ((int)(sizeof(INFO_ACTIONS) / sizeof(INFO_ACTIONS[0])))

#define ANALYZER_CENTER_HZ 433920000
#define ANALYZER_SPAN_HZ   2000000
#define ANALYZER_POLL_MS   140
#define ANALYZER_DBM_FLOOR -110
#define ANALYZER_DBM_CEIL  -20

typedef enum {
  VIEW_LIST = 0,
  VIEW_ANALYZER,
  VIEW_SAVED,
  VIEW_SAVED_INFO,
} view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_freq_timer = NULL;
static view_t s_view = VIEW_LIST;
static int s_saved_sel = 0;

static lv_obj_t *s_freq_lbl = NULL;

static lv_obj_t *s_info_rows[INFO_ACTION_COUNT];
static lv_obj_t *s_info_icons[INFO_ACTION_COUNT];
static lv_obj_t *s_info_labels[INFO_ACTION_COUNT];
static int s_info_sel = 0;

static lv_obj_t *s_sig_cont = NULL;
static lv_obj_t *s_sig_row[SAVED_MAX];
static lv_obj_t *s_sig_name[SAVED_MAX];
static lv_obj_t *s_sig_val[SAVED_MAX];
static lv_obj_t *s_sig_thumb = NULL;
static lv_obj_t *s_spec_bars[BAR_COUNT];

static lv_obj_t *s_send_overlay = NULL;
static lv_obj_t *s_send_status = NULL;
static lv_timer_t *s_send_t1 = NULL;
static lv_timer_t *s_send_t2 = NULL;

static void subghz_menu_input(const input_event_t *ev, void *ctx);
static void build_screen(void);

static void rebuild_async(void *p) {
  (void)p;
  build_screen();
}

static void stop_freq_timer(void) {
  if (s_freq_timer != NULL) {
    lv_timer_delete(s_freq_timer);
    s_freq_timer = NULL;
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

#define SIGNAL_STRONG_COLOR 0x00E676

static void spectrum_poll_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen || s_view != VIEW_ANALYZER) {
    lv_timer_delete(t);
    s_freq_timer = NULL;
    return;
  }

  subghz_spectrum_line_t line;
  if (!subghz_spectrum_get_line(&line))
    return;

  int per = SPECTRUM_SAMPLES / BAR_COUNT;
  if (per < 1)
    per = 1;

  for (int i = 0; i < BAR_COUNT; i++) {
    float sum = 0.0f;
    int cnt = 0;
    for (int k = 0; k < per; k++) {
      int idx = i * per + k;
      if (idx < SPECTRUM_SAMPLES) {
        sum += line.dbm_values[idx];
        cnt++;
      }
    }
    float dbm = cnt ? (sum / (float)cnt) : (float)ANALYZER_DBM_FLOOR;

    int h = (int)(((dbm - ANALYZER_DBM_FLOOR) * (BAR_MAX_H - BAR_MIN_H)) /
                  (ANALYZER_DBM_CEIL - ANALYZER_DBM_FLOOR)) +
            BAR_MIN_H;
    if (h < BAR_MIN_H)
      h = BAR_MIN_H;
    if (h > BAR_MAX_H)
      h = BAR_MAX_H;

    if (s_spec_bars[i] == NULL)
      continue;
    lv_obj_set_height(s_spec_bars[i], h);
    lv_obj_set_y(s_spec_bars[i], BAR_BASELINE_Y - h);

    lv_color_t c;
    if (dbm >= -45.0f)
      c = lv_color_hex(SIGNAL_STRONG_COLOR);
    else if (dbm >= -75.0f)
      c = current_theme.border_accent;
    else
      c = current_theme.border_inactive;
    lv_obj_set_style_bg_color(s_spec_bars[i], c, 0);
  }
}

static void build_analyzer(void) {
  ui_chrome_header(s_screen, "ANALYZER", "/assets/icons/graphic_eq.bin");

  char freq_str[16];
  fmt_mhz(ANALYZER_CENTER_HZ, freq_str, sizeof(freq_str));
  s_freq_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_freq_lbl, freq_str);
  lv_obj_set_style_text_color(s_freq_lbl, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_freq_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_freq_lbl, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 14);

  int total_w = BAR_COUNT * BAR_W + (BAR_COUNT - 1) * BAR_GAP;
  int x0 = (ui_screen_w() - total_w) / 2;

  static lv_point_precise_t base_pts[2];
  base_pts[0].x = 0;
  base_pts[0].y = 0;
  base_pts[1].x = total_w;
  base_pts[1].y = 0;
  lv_obj_t *baseline = lv_line_create(s_screen);
  lv_line_set_points(baseline, base_pts, 2);
  lv_obj_set_pos(baseline, x0, BAR_BASELINE_Y);
  lv_obj_set_style_line_color(baseline, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(baseline, LV_OPA_50, 0);
  lv_obj_set_style_line_width(baseline, 2, 0);
  lv_obj_set_style_line_dash_width(baseline, 4, 0);
  lv_obj_set_style_line_dash_gap(baseline, 4, 0);

  for (int i = 0; i < BAR_COUNT; i++) {
    lv_obj_t *bar = lv_obj_create(s_screen);
    s_spec_bars[i] = bar;
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, BAR_W, BAR_MIN_H);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, current_theme.border_inactive, 0);
    lv_obj_set_style_bg_grad_color(bar, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_pos(bar, x0 + i * (BAR_W + BAR_GAP), BAR_BASELINE_Y - BAR_MIN_H);
  }

  fade_in(s_freq_lbl, FADE_MS);

  if (subghz_receiver_is_running())
    subghz_receiver_stop();
  subghz_spectrum_start(ANALYZER_CENTER_HZ, ANALYZER_SPAN_HZ);
  s_freq_timer = lv_timer_create(spectrum_poll_cb, ANALYZER_POLL_MS, NULL);

  ui_chrome_footer(s_screen, HINT_ANALYZER);
}

static void build_saved_empty(void) {
  ui_chrome_header(s_screen, "SAVED", "/assets/icons/folder.bin");
  octobit_create(s_screen, "No saved signals yet");
  ui_chrome_footer(s_screen, "BACK exit");
}

static void move_sig_thumb(void) {
  if (s_sig_thumb == NULL || s_saved_count <= 1)
    return;
  int thumb_h = lv_obj_get_height(s_sig_thumb);
  if (thumb_h <= 0)
    thumb_h = SGC_THUMB_H;
  int travel = SGC_TRACK_LEN - thumb_h;
  if (travel < 0)
    travel = 0;
  int pos = SGC_TRACK_Y + (s_saved_sel * travel) / (s_saved_count - 1);
  lv_obj_set_y(s_sig_thumb, pos);
}

static void style_sig_row(int i, bool sel) {
  lv_obj_t *card = s_sig_row[i];
  if (sel) {
    lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(card, SGC_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(card, -2, 0);
    lv_obj_set_style_text_color(s_sig_val[i], current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_sig_val[i], current_theme.text_secondary, 0);
  }
}

static void update_sig_selection(void) {
  for (int i = 0; i < s_saved_count; i++)
    style_sig_row(i, i == s_saved_sel);
  if (s_sig_cont != NULL && s_sig_row[s_saved_sel] != NULL) {
    lv_obj_update_layout(s_sig_cont);
    lv_obj_scroll_to_view(s_sig_row[s_saved_sel], LV_ANIM_ON);
  }
  move_sig_thumb();
}

static void build_saved_list(void) {
  load_saved();
  if (s_saved_count == 0) {
    build_saved_empty();
    return;
  }
  ui_chrome_header(s_screen, "SAVED", "/assets/icons/folder.bin");

  if (s_saved_sel < 0)
    s_saved_sel = 0;
  if (s_saved_sel >= s_saved_count)
    s_saved_sel = s_saved_count - 1;

  lv_obj_t *cont = lv_obj_create(s_screen);
  s_sig_cont = cont;
  lv_obj_set_size(cont,
                  ui_screen_w() - SGC_LEFT - SGC_GUTTER,
                  ui_screen_h() - SGC_TOP_Y - UI_CHROME_FOOTER_H - 4);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, SGC_LEFT, SGC_TOP_Y);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, SGC_LIST_PAD, 0);
  lv_obj_set_style_pad_row(cont, SGC_LIST_ROW, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);

  for (int i = 0; i < s_saved_count; i++) {
    lv_obj_t *card = lv_obj_create(cont);
    s_sig_row[i] = card;
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(card, lv_pct(100), SGC_CARD_H);
    lv_obj_set_style_radius(card, SGC_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(card, SGC_CARD_PAD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_color(card, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);

    lv_obj_t *name = lv_label_create(card);
    s_sig_name[i] = name;
    lv_obj_set_width(name, lv_pct(68));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, s_saved[i].name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, current_theme.text_main, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    char fbuf[16];
    fmt_mhz(s_saved[i].frequency, fbuf, sizeof(fbuf));
    lv_obj_t *val = lv_label_create(card);
    s_sig_val[i] = val;
    lv_label_set_text(val, fbuf);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(val, current_theme.border_accent, 0);
    lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, 2);

    lv_obj_t *proto = lv_label_create(card);
    lv_obj_set_width(proto, lv_pct(100));
    lv_label_set_long_mode(proto, LV_LABEL_LONG_DOT);
    lv_label_set_text(proto, s_saved[i].protocol);
    lv_obj_set_style_text_font(proto, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(proto, current_theme.text_secondary, 0);
    lv_obj_align(proto, LV_ALIGN_TOP_LEFT, 0, 20);

    lv_obj_t *bars = lv_obj_create(card);
    lv_obj_remove_flag(bars, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bars, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bars, lv_pct(100), SGC_BARS_H);
    lv_obj_align(bars, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(bars, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bars, 0, 0);
    lv_obj_set_style_pad_all(bars, 0, 0);
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (int k = 0; k < SGC_BARS; k++) {
      lv_obj_t *bar = lv_obj_create(bars);
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
      int h = SGC_BAR_MIN + ((i * 7 + k * 13) % SGC_BAR_SPAN);
      lv_obj_set_size(bar, SGC_BAR_W, h);
      lv_obj_set_style_radius(bar, 1, 0);
      lv_obj_set_style_border_width(bar, 0, 0);
      lv_obj_set_style_bg_color(bar, current_theme.border_accent, 0);
      lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0xB89AFF), 0);
      lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
  }

  static lv_point_precise_t sg_track_pts[2];
  sg_track_pts[0].x = 0;
  sg_track_pts[0].y = 0;
  sg_track_pts[1].x = 0;
  sg_track_pts[1].y = SGC_TRACK_LEN;
  lv_obj_t *track = lv_line_create(s_screen);
  lv_line_set_points(track, sg_track_pts, 2);
  lv_obj_set_pos(track, SGC_TRACK_X, SGC_TRACK_Y);
  lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_width(track, 3, 0);
  lv_obj_set_style_line_dash_width(track, 4, 0);
  lv_obj_set_style_line_dash_gap(track, 4, 0);

  lv_image_dsc_t *thumb = assets_get(SGC_THUMB_ICON);
  s_sig_thumb = lv_image_create(s_screen);
  if (thumb != NULL)
    lv_image_set_src(s_sig_thumb, thumb);
  lv_obj_set_pos(s_sig_thumb, SGC_TRACK_X - 4, SGC_TRACK_Y);
  lv_obj_move_foreground(s_sig_thumb);

  lv_obj_update_layout(cont);
  update_sig_selection();
  fade_in(cont, FADE_MS);

  ui_chrome_footer(s_screen, HINT_SAVED);
}

static void card_rise_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void style_info_row(int idx, bool sel) {
  lv_obj_t *row = s_info_rows[idx];
  if (sel) {
    lv_obj_set_style_bg_color(row, lv_color_hex(COL_RAISE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, 14, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_50, 0);
    lv_obj_set_style_shadow_spread(row, -3, 0);
    lv_obj_set_style_text_color(s_info_icons[idx], current_theme.border_accent, 0);
    lv_obj_set_style_text_color(s_info_labels[idx], current_theme.text_main, 0);
  } else {
    lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_80, 0);
    lv_obj_set_style_border_color(row, current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_info_icons[idx], current_theme.text_secondary, 0);
    lv_obj_set_style_text_color(s_info_labels[idx], current_theme.text_main, 0);
  }
}

static void update_info_selection(void) {
  for (int i = 0; i < INFO_ACTION_COUNT; i++)
    style_info_row(i, i == s_info_sel);
}

static void build_saved_info(void) {
  ui_chrome_header(s_screen, "SAVED", "/assets/icons/folder.bin");

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_CTR_Y);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 14, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -3, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 4, 0);

  lv_obj_t *name = lv_label_create(card);
  lv_label_set_text(name, s_saved[s_saved_sel].name);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);

  char freq_str[16];
  fmt_mhz(s_saved[s_saved_sel].frequency, freq_str, sizeof(freq_str));

  char key_str[24];
  strlcpy(key_str, "RAW", sizeof(key_str));
  char content[512];
  if (subghz_storage_read(s_saved[s_saved_sel].name, content, sizeof(content)) > 0) {
    const char *kp = strstr(content, "Key: ");
    if (kp != NULL) {
      unsigned b[8] = {0};
      if (sscanf(kp + 5,
                 "%x %x %x %x %x %x %x %x",
                 &b[0],
                 &b[1],
                 &b[2],
                 &b[3],
                 &b[4],
                 &b[5],
                 &b[6],
                 &b[7]) >= 8) {
        uint32_t v = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) |
                     (uint32_t)b[7];
        snprintf(key_str, sizeof(key_str), "0x%lX", (unsigned long)v);
      }
    }
  }

  const struct {
    const char *label;
    const char *value;
  } rows[] = {
      {"Protocol", s_saved[s_saved_sel].protocol},
      {"Key", key_str},
      {"Frequency", freq_str},
  };

  for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, rows[i].label);
    lv_obj_set_style_text_color(label, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);

    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, rows[i].value);
    lv_obj_set_style_text_color(value, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
  }

  lv_obj_t *acts = lv_obj_create(s_screen);
  lv_obj_remove_flag(acts, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(acts, INFO_ACT_W, LV_SIZE_CONTENT);
  lv_obj_align(acts, LV_ALIGN_BOTTOM_MID, 0, INFO_ACT_BOTTOM_Y);
  lv_obj_set_style_bg_opa(acts, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(acts, 0, 0);
  lv_obj_set_style_pad_all(acts, 0, 0);
  lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(acts, INFO_ACT_GAP, 0);

  for (int i = 0; i < INFO_ACTION_COUNT; i++) {
    lv_obj_t *row = lv_obj_create(acts);
    s_info_rows[i] = row;
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), INFO_ACT_H);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t *icon = lv_label_create(row);
    s_info_icons[i] = icon;
    lv_label_set_text(icon, INFO_ACTIONS[i].icon);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);

    lv_obj_t *label = lv_label_create(row);
    s_info_labels[i] = label;
    lv_label_set_text(label, INFO_ACTIONS[i].label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  }
  s_info_sel = 0;
  update_info_selection();

  ui_chrome_footer(s_screen, HINT_INFO);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_exec_cb(&a, card_rise_cb);
  lv_anim_set_values(&a, CARD_RISE_PX, 0);
  lv_anim_set_duration(&a, CARD_RISE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void info_del_confirm(bool confirm) {
  if (!confirm)
    return;
  esp_err_t err = subghz_storage_delete(s_saved[s_saved_sel].name);
  ui_feedback(UI_FB_WRITE);
  notify(err == ESP_OK ? NOTIFY_INFO : NOTIFY_WARNING,
         err == ESP_OK ? "Signal deleted" : "Delete failed");
  s_view = VIEW_SAVED;
  ui_async_call(rebuild_async, NULL);
}

static void build_screen(void) {
  stop_freq_timer();
  subghz_spectrum_stop();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_freq_lbl = NULL;
  s_sig_cont = NULL;
  s_sig_thumb = NULL;
  if (s_send_t1 != NULL) {
    lv_timer_delete(s_send_t1);
    s_send_t1 = NULL;
  }
  if (s_send_t2 != NULL) {
    lv_timer_delete(s_send_t2);
    s_send_t2 = NULL;
  }
  subghz_scope_stop();
  s_send_overlay = NULL;
  s_send_status = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_view) {
    case VIEW_ANALYZER:
      build_analyzer();
      break;
    case VIEW_SAVED:
      build_saved_list();
      break;
    case VIEW_SAVED_INFO:
      build_saved_info();
      break;
    case VIEW_LIST:
    default:
      s_menu =
          menu_component_create(s_screen, "SUB-GHZ", "/assets/icons/settings_input_antenna.bin");
      for (int i = 0; i < ITEM_COUNT; i++)
        menu_component_add_item(&s_menu, ITEMS[i].icon, ITEMS[i].name);
      fade_in(s_menu.items_cont, FADE_MS);
      fade_in(s_menu.title_bar, FADE_MS);
      break;
  }

  ui_input_set_screen_handler(subghz_menu_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void send_finish_cb(lv_timer_t *t) {
  lv_timer_delete(t);
  s_send_t2 = NULL;
  subghz_scope_stop();
  if (s_send_overlay != NULL) {
    lv_obj_del(s_send_overlay);
    s_send_overlay = NULL;
    s_send_status = NULL;
  }
  notify(NOTIFY_INFO, "Signal sent");
}

static void send_lock_cb(lv_timer_t *t) {
  lv_timer_delete(t);
  s_send_t1 = NULL;
  if (lv_screen_active() != s_screen) {
    subghz_scope_stop();
    return;
  }
  subghz_scope_lock();
  if (s_send_status != NULL) {
    lv_label_set_text(s_send_status, "Signal sent!");
    lv_obj_set_style_text_color(s_send_status, lv_color_hex(SIGNAL_STRONG_COLOR), 0);
  }
  ui_feedback(UI_FB_WRITE);
  s_send_t2 = lv_timer_create(send_finish_cb, SEND_DONE_MS, NULL);
  lv_timer_set_repeat_count(s_send_t2, 1);
}

static void start_saved_send(void) {
  esp_err_t rerr = subghz_replay_file(s_saved[s_saved_sel].name);
  if (rerr == ESP_ERR_NOT_SUPPORTED) {
    notify(NOTIFY_WARNING, "Replay not supported");
    return;
  }
  if (rerr != ESP_OK) {
    notify(NOTIFY_WARNING, "Send failed");
    return;
  }

  s_send_overlay = lv_obj_create(s_screen);
  lv_obj_set_size(s_send_overlay, lv_pct(100), lv_pct(100));
  lv_obj_center(s_send_overlay);
  lv_obj_remove_flag(s_send_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_send_overlay, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_send_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_send_overlay, 0, 0);
  lv_obj_set_style_pad_all(s_send_overlay, 0, 0);

  ui_chrome_header_overlay(s_send_overlay, "SEND", "/assets/icons/settings_input_antenna.bin");

  s_send_status = lv_label_create(s_send_overlay);
  lv_label_set_text(s_send_status, "Transmitting...");
  lv_obj_set_style_text_color(s_send_status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_send_status, &lv_font_montserrat_14, 0);
  lv_obj_align(s_send_status, LV_ALIGN_TOP_MID, 0, SEND_STATUS_Y);

  char freq_str[16];
  fmt_mhz(s_saved[s_saved_sel].frequency, freq_str, sizeof(freq_str));
  lv_obj_t *freq = lv_label_create(s_send_overlay);
  lv_label_set_text(freq, freq_str);
  lv_obj_set_style_text_color(freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(freq, &lv_font_montserrat_12, 0);
  lv_obj_align(freq, LV_ALIGN_TOP_MID, 0, SEND_FREQ_Y);

  subghz_scope_create(s_send_overlay, LV_ALIGN_CENTER, 0, SEND_SCOPE_Y);
  ui_chrome_footer(s_send_overlay, "Transmitting...");

  ui_feedback(UI_FB_EMULATE);
  s_send_t1 = lv_timer_create(send_lock_cb, SEND_ANIM_MS, NULL);
  lv_timer_set_repeat_count(s_send_t1, 1);
}

static void subghz_menu_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_send_overlay != NULL)
    return;

  switch (s_view) {
    case VIEW_LIST:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav)
            menu_component_next(&s_menu);
          break;
        case INPUT_BTN_UP:
          if (nav)
            menu_component_prev(&s_menu);
          break;
        case INPUT_BTN_OK:
          if (press) {
            int sel = menu_component_get_selected(&s_menu);
            if (sel == IDX_ANALYZER) {
              s_view = VIEW_ANALYZER;
              build_screen();
            } else if (sel == IDX_SAVED) {
              s_saved_sel = 0;
              s_view = VIEW_SAVED;
              build_screen();
            } else if (sel == IDX_BRUTE) {
              ui_switch_screen(SCREEN_SUBGHZ_BRUTE);
            } else if (sel == IDX_SEND) {
              ui_switch_screen(SCREEN_SUBGHZ_SEND);
            } else if (sel == IDX_CONFIG) {
              ui_switch_screen(SCREEN_SUBGHZ_CONFIG);
            } else if (sel >= 0 && sel < ITEM_COUNT && ITEMS[sel].capture) {
              ui_switch_screen(SCREEN_SUBGHZ_READ);
            }
          }
          break;
        case INPUT_BTN_BACK:
          if (press)
            ui_switch_screen(SCREEN_MENU);
          break;
        default:
          break;
      }
      break;

    case VIEW_ANALYZER:
      switch (ev->button) {
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_LIST;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;

    case VIEW_SAVED:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_saved_sel < s_saved_count - 1) {
            s_saved_sel++;
            update_sig_selection();
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_UP:
          if (nav && s_saved_sel > 0) {
            s_saved_sel--;
            update_sig_selection();
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_OK:
          if (press) {
            s_view = VIEW_SAVED_INFO;
            build_screen();
          }
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_LIST;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;

    case VIEW_SAVED_INFO:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_info_sel < INFO_ACTION_COUNT - 1) {
            s_info_sel++;
            update_info_selection();
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_UP:
          if (nav && s_info_sel > 0) {
            s_info_sel--;
            update_info_selection();
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_OK:
          if (press) {
            if (s_info_sel == 0) {
              start_saved_send();
            } else {
              msgbox_open(LV_SYMBOL_TRASH, "Delete signal?", "Delete", "Cancel", info_del_confirm);
            }
          }
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_SAVED;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;
  }
}

void ui_subghz_menu_open(void) {
  s_view = VIEW_LIST;
  s_saved_sel = 0;
  build_screen();
}

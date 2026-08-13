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

#include "lora_traceroute_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "LORA_TRACERT";

#define NAV_TIMER_MS   50
#define TRACE_MS       1600
#define ENTRY_MS       220
#define ROW_STAGGER_MS 90

#define SIG_GREEN 0x00E676
#define COL_DIM   0x8A8594

#define BODY_TOP_Y  46
#define HOP_LIST_W  228
#define HOP_ROW_W   214
#define HOP_ROW_H   46
#define HOP_ROW_GAP 8
#define HOP_RADIUS  12
#define HOP_PAD     9

#define PANEL_SHADOW_W      14
#define PANEL_SHADOW_SPREAD (-3)

#define IDX_BADGE  26
#define IDX_RADIUS 8

#define ICON_HUB     "/assets/icons/hub.bin"
#define WAVES_Y_OFS  (-10)
#define STATUS_Y_OFS 96
#define RISE_PX      24

#define SNR_JITTER_SPAN 11
#define SNR_JITTER_BIAS 5
#define SNR_GOOD_MIN    0

static const struct {
  const char *name;
  int snr_tenths;
} HOPS[] = {
    {"Base Camp", 105},
    {"Gateway-1", 75},
    {"Relay-7", -35},
    {"Trekker", -75},
};
#define HOP_COUNT ((int)(sizeof(HOPS) / sizeof(HOPS[0])))

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_trace_timer = NULL;
static bool s_tracing = true;
static int s_snr[HOP_COUNT];

static bool s_left_last, s_back_last;

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);

static void stop_trace_timer(void) {
  if (s_trace_timer != NULL) {
    lv_timer_delete(s_trace_timer);
    s_trace_timer = NULL;
  }
}

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void transy_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void reveal_row(lv_obj_t *o, int idx) {
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);

  lv_anim_t ao;
  lv_anim_init(&ao);
  lv_anim_set_var(&ao, o);
  lv_anim_set_exec_cb(&ao, opa_cb);
  lv_anim_set_values(&ao, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&ao, ENTRY_MS);
  lv_anim_set_delay(&ao, idx * ROW_STAGGER_MS);
  lv_anim_start(&ao);

  lv_anim_t ay;
  lv_anim_init(&ay);
  lv_anim_set_var(&ay, o);
  lv_anim_set_exec_cb(&ay, transy_cb);
  lv_anim_set_values(&ay, RISE_PX, 0);
  lv_anim_set_duration(&ay, ENTRY_MS);
  lv_anim_set_delay(&ay, idx * ROW_STAGGER_MS);
  lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
  lv_anim_start(&ay);
}

static void fmt_snr(char *buf, size_t len, int tenths) {
  const char *sign = (tenths < 0) ? "-" : "";
  int mag = (tenths < 0) ? -tenths : tenths;
  snprintf(buf, len, "%s%d.%d dB", sign, mag / 10, mag % 10);
}

static const char *hop_role(int idx) {
  if (idx == 0)
    return "origin";
  if (idx == HOP_COUNT - 1)
    return "target";
  return "relay";
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h, lv_color_t accent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, HOP_RADIUS, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, accent, 0);
  lv_obj_set_style_shadow_color(p, accent, 0);
  lv_obj_set_style_shadow_width(p, PANEL_SHADOW_W, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, PANEL_SHADOW_SPREAD, 0);
  return p;
}

static lv_obj_t *make_index_badge(lv_obj_t *parent, int number, lv_color_t accent) {
  lv_obj_t *badge = lv_obj_create(parent);
  lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(badge, IDX_BADGE, IDX_BADGE);
  lv_obj_set_style_radius(badge, IDX_RADIUS, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_set_style_bg_color(badge, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_border_color(badge, accent, 0);

  lv_obj_t *l = lv_label_create(badge);
  lv_label_set_text_fmt(l, "%d", number);
  lv_obj_set_style_text_color(l, accent, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_center(l);
  return badge;
}

static void make_hop_row(lv_obj_t *parent, int idx, lv_color_t accent) {
  bool good = s_snr[idx] >= SNR_GOOD_MIN;
  lv_color_t edge = good ? lv_color_hex(SIG_GREEN) : accent;

  lv_obj_t *row = lit_panel(parent, HOP_ROW_W, HOP_ROW_H, edge);
  lv_obj_set_style_pad_all(row, HOP_PAD, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 9, 0);

  make_index_badge(row, idx + 1, edge);

  lv_obj_t *txt = lv_obj_create(row);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(txt, 1);

  lv_obj_t *nm = lv_label_create(txt);
  lv_label_set_text(nm, HOPS[idx].name);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);

  lv_obj_t *sub = lv_label_create(txt);
  lv_label_set_text(sub, hop_role(idx));
  lv_obj_set_style_text_color(sub, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);

  lv_obj_t *snr = lv_label_create(row);
  char buf[16];
  fmt_snr(buf, sizeof(buf), s_snr[idx]);
  lv_label_set_text(snr, buf);
  lv_obj_set_style_text_color(snr, good ? lv_color_hex(SIG_GREEN) : lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(snr, &lv_font_montserrat_14, 0);
}

static void build_tracing(void) {
  ui_chrome_header(s_screen, "TRACEROUTE", ICON_HUB);

  waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y_OFS, LV_SYMBOL_GPS, ICON_HUB);

  lv_obj_t *status = lv_label_create(s_screen);
  char buf[48];
  snprintf(buf, sizeof(buf), "Tracing to %s...", HOPS[HOP_COUNT - 1].name);
  lv_label_set_text(status, buf);
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, STATUS_Y_OFS);

  ui_chrome_footer(s_screen, "BACK to cancel");
}

static void build_result(void) {
  ui_chrome_header(s_screen, "TRACEROUTE", ICON_HUB);

  lv_obj_t *list = lv_obj_create(s_screen);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(list, HOP_LIST_W, LV_SIZE_CONTENT);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, BODY_TOP_Y);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, HOP_ROW_GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < HOP_COUNT; i++)
    make_hop_row(list, i, ui_theme_get_accent());
  for (int i = 0; i < HOP_COUNT; i++)
    reveal_row(lv_obj_get_child(list, i), i);

  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT " BACK to LoRa");
}

static void trace_done_cb(lv_timer_t *t) {
  (void)t;
  s_trace_timer = NULL;
  if (lv_screen_active() != s_screen || !s_tracing)
    return;
  s_tracing = false;
  ui_feedback(UI_FB_READ);
  build_screen();
}

static void build_screen(void) {
  stop_trace_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  if (s_tracing) {
    build_tracing();
    s_trace_timer = lv_timer_create(trace_done_cb, TRACE_MS, NULL);
    lv_timer_set_repeat_count(s_trace_timer, 1);
  } else {
    build_result();
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    stop_trace_timer();
    return;
  }

  bool left = ui_btn_left();
  bool back = back_button_is_down();

  if (ui_input_is_locked()) {
    s_left_last = left;
    s_back_last = back;
    return;
  }

  if ((back && !s_back_last) || (left && !s_left_last))
    ui_switch_screen(SCREEN_LORA_CHAT);

  s_left_last = left;
  s_back_last = back;
}

void ui_lora_traceroute_open(void) {
  ui_theme_set_protocol(PROTOCOL_LORA);
  s_tracing = true;
  s_trace_timer = NULL;
  s_left_last = false;
  s_back_last = false;

  for (int i = 0; i < HOP_COUNT; i++) {
    int jitter = (int)(esp_random() % SNR_JITTER_SPAN) - SNR_JITTER_BIAS;
    s_snr[i] = HOPS[i].snr_tenths + jitter;
  }

  build_screen();
  ESP_LOGI(TAG, "LoRa traceroute (mock) opened");
}

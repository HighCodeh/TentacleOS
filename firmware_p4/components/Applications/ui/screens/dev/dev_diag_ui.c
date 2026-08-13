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

#include "dev_diag_ui.h"

#include <stdio.h>

#include "lvgl.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50
#define TICK_MS      260

#define TITLE       "DIAGNOSTICS"
#define ICON        "/assets/icons/troubleshoot.bin"
#define FOOTER_HINT LV_SYMBOL_LEFT "  BACK exit"

#define OK_COLOR   0x00E676
#define WARN_COLOR 0xFFC23D
#define CYAN_COLOR 0x37E0A8
#define DIM_COLOR  0x8A8594
#define GRID_COLOR 0x241F31

#define PANEL_PAD    8
#define PANEL_RAD    11
#define CHART_POINTS 30
#define CHART_H      44
#define STATS_H      40

#define HEAP_BASE    56
#define HEAP_SPAN    12
#define CPU_MIN      18
#define CPU_SPAN     62
#define HEAP_KB_BASE 150

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav = NULL;
static lv_timer_t *s_tick = NULL;

static lv_obj_t *s_heap_chart = NULL;
static lv_chart_series_t *s_heap_ser = NULL;
static lv_obj_t *s_cpu_chart = NULL;
static lv_chart_series_t *s_cpu_ser = NULL;
static lv_obj_t *s_heap_val = NULL;
static lv_obj_t *s_cpu_val = NULL;

static uint32_t s_seed = 0x1234abcdu;
static int s_phase = 0;
static bool s_back_last = false;

static lv_obj_t *make_panel(lv_obj_t *parent, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, lv_pct(100), h);
  lv_obj_set_style_radius(p, PANEL_RAD, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(p, PANEL_PAD, 0);
  lv_obj_set_style_pad_row(p, 4, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return p;
}

static lv_obj_t *
make_value_label(lv_obj_t *panel, const char *title, uint32_t color, const char *initial) {
  lv_obj_t *head = lv_obj_create(panel);
  lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(head, 0, 0);
  lv_obj_set_style_pad_all(head, 0, 0);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *t = lv_label_create(head);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, lv_color_hex(DIM_COLOR), 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);

  lv_obj_t *v = lv_label_create(head);
  lv_label_set_text(v, initial);
  lv_obj_set_style_text_color(v, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
  return v;
}

static void style_chart(lv_obj_t *chart, uint32_t line_color, bool fill) {
  lv_obj_set_size(chart, lv_pct(100), CHART_H);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_all(chart, 0, 0);
  lv_obj_set_style_line_color(chart, lv_color_hex(GRID_COLOR), LV_PART_MAIN);
  lv_obj_set_style_line_opa(chart, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(chart, CHART_POINTS);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(chart, 2, 0);
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  if (fill) {
    lv_obj_set_style_bg_color(chart, lv_color_hex(line_color), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(chart, LV_OPA_20, LV_PART_ITEMS);
  } else {
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_ITEMS);
  }
}

static lv_obj_t *make_stat(lv_obj_t *row, const char *key, const char *val, uint32_t color) {
  lv_obj_t *c = lv_obj_create(row);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(c, 1);
  lv_obj_set_height(c, lv_pct(100));
  lv_obj_set_style_radius(c, 8, 0);
  lv_obj_set_style_bg_color(c, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(c, 5, 0);
  lv_obj_set_style_pad_row(c, 1, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *k = lv_label_create(c);
  lv_label_set_text(k, key);
  lv_obj_set_style_text_color(k, lv_color_hex(DIM_COLOR), 0);
  lv_obj_set_style_text_font(k, &lv_font_montserrat_12, 0);

  lv_obj_t *v = lv_label_create(c);
  lv_label_set_text(v, val);
  lv_obj_set_style_text_color(v, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
  return c;
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_tick = NULL;
    return;
  }
  s_phase = (s_phase + 11) % 360;
  int heap = HEAP_BASE + (HEAP_SPAN * (lv_trigo_sin((int16_t)s_phase) + 32767)) / 65534;
  s_seed = s_seed * 1103515245u + 12345u;
  int cpu = CPU_MIN + (int)((s_seed >> 16) % (uint32_t)CPU_SPAN);

  if (s_heap_ser != NULL)
    lv_chart_set_next_value(s_heap_chart, s_heap_ser, heap);
  if (s_cpu_ser != NULL)
    lv_chart_set_next_value(s_cpu_chart, s_cpu_ser, cpu);

  char buf[16];
  if (s_heap_val != NULL) {
    snprintf(buf, sizeof(buf), "%d KB", HEAP_KB_BASE + heap);
    lv_label_set_text(s_heap_val, buf);
  }
  if (s_cpu_val != NULL) {
    snprintf(buf, sizeof(buf), "%d%%", cpu);
    lv_label_set_text(s_cpu_val, buf);
  }
}

static void nav_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;
  bool back = back_button_is_down();
  bool left = ui_btn_left();
  if ((back && !s_back_last) || left)
    ui_switch_screen(SCREEN_DEV_MENU);
  s_back_last = back;
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

  lv_obj_t *col = lv_obj_create(s_screen);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(col, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 6);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_hor(col, 10, 0);
  lv_obj_set_style_pad_row(col, 8, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *heap_panel = make_panel(col, CHART_H + 40);
  s_heap_val = make_value_label(heap_panel, "Heap free", 0xB89AFF, "206 KB");
  s_heap_chart = lv_chart_create(heap_panel);
  style_chart(s_heap_chart, 0xB89AFF, true);
  s_heap_ser = lv_chart_add_series(s_heap_chart, lv_color_hex(0xB89AFF), LV_CHART_AXIS_PRIMARY_Y);

  lv_obj_t *cpu_panel = make_panel(col, CHART_H + 40);
  s_cpu_val = make_value_label(cpu_panel, "CPU load", CYAN_COLOR, "41%");
  s_cpu_chart = lv_chart_create(cpu_panel);
  style_chart(s_cpu_chart, CYAN_COLOR, false);
  s_cpu_ser = lv_chart_add_series(s_cpu_chart, lv_color_hex(CYAN_COLOR), LV_CHART_AXIS_PRIMARY_Y);

  for (int i = 0; i < CHART_POINTS; i++) {
    lv_chart_set_next_value(s_heap_chart, s_heap_ser, HEAP_BASE);
    lv_chart_set_next_value(s_cpu_chart, s_cpu_ser, CPU_MIN);
  }

  lv_obj_t *stats = lv_obj_create(col);
  lv_obj_remove_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(stats, lv_pct(100), STATS_H);
  lv_obj_set_style_bg_opa(stats, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(stats, 0, 0);
  lv_obj_set_style_pad_all(stats, 0, 0);
  lv_obj_set_style_pad_column(stats, 6, 0);
  lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_stat(stats, "Batt", "84%", OK_COLOR);
  make_stat(stats,
            "Temp",
            "41\xC2\xB0"
            "C",
            CYAN_COLOR);
  make_stat(stats, "C5", "OFF", WARN_COLOR);

  ui_screen_load(s_screen);
}

void ui_dev_diag_open(void) {
  s_nav = NULL;
  s_tick = NULL;
  s_phase = 0;
  s_back_last = false;
  build_screen();
  s_nav = lv_timer_create(nav_cb, NAV_TIMER_MS, NULL);
  s_tick = lv_timer_create(tick_cb, TICK_MS, NULL);
}

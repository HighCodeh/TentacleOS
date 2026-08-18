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

#include "esp_heap_caps.h"
#include "lvgl.h"

#include "battery_service.h"
#include "spi_bridge.h"
#include "sys_metrics.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define TICK_MS 260

#define TITLE       "DIAGNOSTICS"
#define ICON        "/assets/icons/troubleshoot.bin"
#define FOOTER_HINT LV_SYMBOL_LEFT "  BACK exit"

#define WARN_COLOR 0xFFC23D
#define CYAN_COLOR 0x37E0A8
#define GRID_COLOR 0x241F31

#define PANEL_PAD    8
#define PANEL_RAD    11
#define CHART_POINTS 30
#define CHART_H      44
#define STATS_H      40

#define INT_MAX_KB     256
#define DMA_MAX_KB     64
#define TEMP_MIN_C     10
#define TEMP_MAX_C     80
#define TEMP_INVALID_C (-300.0f)
#define BATT_LOW_PCT   20

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_tick = NULL;

static lv_obj_t *s_heap_chart = NULL;
static lv_chart_series_t *s_heap_ser = NULL;
static lv_obj_t *s_dma_chart = NULL;
static lv_chart_series_t *s_dma_ser = NULL;
static lv_obj_t *s_heap_val = NULL;
static lv_obj_t *s_dma_val = NULL;
static lv_obj_t *s_batt_val = NULL;
static lv_obj_t *s_temp_val = NULL;
static lv_obj_t *s_c5_val = NULL;

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
  lv_obj_set_style_text_color(t, current_theme.text_secondary, 0);
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
  lv_obj_set_style_text_color(k, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(k, &lv_font_montserrat_12, 0);

  lv_obj_t *v = lv_label_create(c);
  lv_label_set_text(v, val);
  lv_obj_set_style_text_color(v, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
  return v;
}

static float read_die_temp(void) {
  float celsius = TEMP_INVALID_C;
  if (!sys_metrics_die_temp_c(&celsius)) {
    return TEMP_INVALID_C;
  }
  return celsius;
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_tick = NULL;
    return;
  }

  int int_kb = (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  int dma_kb = (int)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024);

  if (s_heap_ser != NULL)
    lv_chart_set_next_value(s_heap_chart, s_heap_ser, int_kb);
  if (s_dma_ser != NULL)
    lv_chart_set_next_value(s_dma_chart, s_dma_ser, dma_kb);

  char buf[24];
  if (s_heap_val != NULL) {
    snprintf(buf, sizeof(buf), "%d KB", int_kb);
    lv_label_set_text(s_heap_val, buf);
  }
  if (s_dma_val != NULL) {
    snprintf(buf, sizeof(buf), "%d KB", dma_kb);
    lv_label_set_text(s_dma_val, buf);
  }

  if (s_batt_val != NULL) {
    battery_snapshot_t b;
    if (battery_service_get(&b) && b.valid) {
      snprintf(buf, sizeof(buf), "%d%%", b.soc);
      lv_obj_set_style_text_color(
          s_batt_val, lv_color_hex(b.soc <= BATT_LOW_PCT ? WARN_COLOR : UI_COL_SUCCESS), 0);
    } else {
      snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_batt_val, buf);
  }

  if (s_temp_val != NULL) {
    float celsius = read_die_temp();
    if (celsius > TEMP_INVALID_C) {
      snprintf(buf,
               sizeof(buf),
               "%d\xC2\xB0"
               "C",
               (int)(celsius + 0.5f));
    } else {
      snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_temp_val, buf);
  }

  if (s_c5_val != NULL) {
    bool alive = spi_bridge_is_alive();
    lv_label_set_text(s_c5_val, alive ? "OK" : "OFF");
    lv_obj_set_style_text_color(s_c5_val, lv_color_hex(alive ? UI_COL_SUCCESS : WARN_COLOR), 0);
  }
}

static void dev_diag_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  switch (ev->button) {
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

  int int_kb = (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  int dma_kb = (int)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024);

  lv_obj_t *heap_panel = make_panel(col, CHART_H + 40);
  s_heap_val = make_value_label(heap_panel, "Internal free", 0xB89AFF, "-- KB");
  s_heap_chart = lv_chart_create(heap_panel);
  style_chart(s_heap_chart, 0xB89AFF, true);
  lv_chart_set_range(s_heap_chart, LV_CHART_AXIS_PRIMARY_Y, 0, INT_MAX_KB);
  s_heap_ser = lv_chart_add_series(s_heap_chart, lv_color_hex(0xB89AFF), LV_CHART_AXIS_PRIMARY_Y);

  lv_obj_t *dma_panel = make_panel(col, CHART_H + 40);
  s_dma_val = make_value_label(dma_panel, "DMA free", CYAN_COLOR, "-- KB");
  s_dma_chart = lv_chart_create(dma_panel);
  style_chart(s_dma_chart, CYAN_COLOR, false);
  lv_chart_set_range(s_dma_chart, LV_CHART_AXIS_PRIMARY_Y, 0, DMA_MAX_KB);
  s_dma_ser = lv_chart_add_series(s_dma_chart, lv_color_hex(CYAN_COLOR), LV_CHART_AXIS_PRIMARY_Y);

  for (int i = 0; i < CHART_POINTS; i++) {
    lv_chart_set_next_value(s_heap_chart, s_heap_ser, int_kb);
    lv_chart_set_next_value(s_dma_chart, s_dma_ser, dma_kb);
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
  s_batt_val = make_stat(stats, "Batt", "--", UI_COL_SUCCESS);
  s_temp_val = make_stat(stats, "Temp", "--", CYAN_COLOR);
  s_c5_val = make_stat(stats, "C5", "--", 0x8A8594);  // TODO: not themed (raw hex arg)

  ui_input_set_screen_handler(dev_diag_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

void ui_dev_diag_open(void) {
  s_tick = NULL;
  build_screen();
  s_tick = lv_timer_create(tick_cb, TICK_MS, NULL);
}

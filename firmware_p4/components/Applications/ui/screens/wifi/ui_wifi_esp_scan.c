// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_wifi_esp_scan.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "button_ui.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "waves_ui.h"
#include "wifi_scan_view.h"

#define SCAN_ICON                "/assets/icons/wifi_find.bin"
#define SCAN_REFRESH_MS          500
#define SCAN_MAX_RECORDS         128
#define SCAN_PADDING             12
#define SCAN_TITLE_Y             12
#define SCAN_SSID_Y              40
#define SCAN_CENTER_OFFSET       (-8)
#define SCAN_RING_MAX            128
#define SCAN_RING_MARGIN         24
#define SCAN_RING_WIDTH          7
#define SCAN_FULL_CIRCLE         360
#define SCAN_ROTATION            270
#define SCAN_MAX_SCORE           100
#define SCAN_DATA_GAP            8
#define SCAN_LINE_HEIGHT         14
#define SCAN_FOOTER_OFFSET       16
#define SCAN_BUTTON_WIDTH_MARGIN 40
#define SCAN_BUTTON_HEIGHT       44
#define SCAN_READY_TITLE_OFFSET  (-52)
#define SCAN_READY_HINT_OFFSET   50
#define SCAN_WAVES_OFFSET        (-14)
#define SCAN_CAPTION_OFFSET      74
#define SCAN_FONT_SCALE          448
#define SCAN_PERCENT_WIDTH       60
#define SCAN_PERCENT_X_OFFSET    (-23)
#define SCAN_PERCENT_Y_OFFSET    16
#define SCAN_TEXT_SIZE           160
#define SCAN_WHITE               0xF1F5F9
#define SCAN_DIM                 0x94A3B8
#define SCAN_CYAN                0x38BDF8
#define SCAN_AMBER               0xFBBF24
#define SCAN_GREEN               0x4ADE80
#define SCAN_TRACK               0x1E293B

typedef enum {
  UI_WIFI_ESP_SCAN_READY,
  UI_WIFI_ESP_SCAN_SEARCHING,
  UI_WIFI_ESP_SCAN_RESULT,
  UI_WIFI_ESP_SCAN_PHASE_COUNT,
} ui_wifi_esp_scan_phase_t;

static const char *TAG = "UI_ESP_SCAN";
static lv_obj_t *s_screen;
static lv_timer_t *s_timer;
static ui_wifi_esp_scan_phase_t s_phase;
static uint16_t s_matches[SCAN_MAX_RECORDS];
static uint16_t s_count, s_index;
static uint32_t s_revision;
static bool s_only_priority = true;
static bool s_is_cancelling;
static int32_t s_width, s_height;

static void build(void);
static void collect_results(void);
static void start_scan(void);
static void refresh(lv_timer_t *timer);
static void on_input(const input_event_t *event, void *ctx);
static lv_obj_t *add_text(const char *text, int32_t y, uint32_t color, const lv_font_t *font);
static void show_result(void);

void ui_wifi_esp_scan_open(void) {
  (void)TAG;
  ui_wifi_esp_scan_stop();
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  wifi_scan_view_status_t status;
  wifi_scan_view_get_status(&status);
  s_phase = status.is_busy ? UI_WIFI_ESP_SCAN_SEARCHING : UI_WIFI_ESP_SCAN_READY;
  s_is_cancelling = status.is_busy;
  build();
  ui_input_set_screen_handler(on_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_timer = lv_timer_create(refresh, SCAN_REFRESH_MS, NULL);
}

void ui_wifi_esp_scan_stop(void) {
  wifi_scan_view_cancel();
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
}

static lv_obj_t *add_text(const char *text, int32_t y, uint32_t color, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(s_screen);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, s_width - SCAN_PADDING * 2);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

static void collect_results(void) {
  wifi_scan_view_status_t status;
  wifi_scan_view_get_status(&status);
  s_count = 0;
  s_index = 0;
  for (uint16_t i = 0; i < status.count && i < SCAN_MAX_RECORDS; i++) {
    spi_wifi_quick_record_t record;
    if (wifi_scan_view_get_record(i, &record) == ESP_OK &&
        (!s_only_priority || record.should_investigate))
      s_matches[s_count++] = i;
  }
}

static void show_result(void) {
  char text[SCAN_TEXT_SIZE];
  snprintf(text,
           sizeof(text),
           "%u/%u | %s",
           s_count ? s_index + 1 : 0,
           s_count,
           s_only_priority ? "ESP" : "TODOS");
  add_text(text, SCAN_TITLE_Y, SCAN_DIM, &lv_font_montserrat_12);
  if (s_count == 0) {
    add_text("Sem redes no filtro", s_height / 2, SCAN_WHITE, &lv_font_montserrat_14);
    return;
  }
  spi_wifi_quick_record_t record;
  if (wifi_scan_view_get_record(s_matches[s_index], &record) != ESP_OK)
    return;
  lv_obj_t *ssid =
      add_text(record.ssid_state == SPI_WIFI_QUICK_SSID_KNOWN ? record.ssid : "Rede oculta",
               SCAN_SSID_Y,
               SCAN_CYAN,
               &lv_font_montserrat_14);
  lv_label_set_long_mode(ssid, LV_LABEL_LONG_SCROLL_CIRCULAR);
  int32_t diameter = LV_MIN(SCAN_RING_MAX, s_height / 2 - SCAN_RING_MARGIN);
  int32_t center = s_height / 2 + SCAN_CENTER_OFFSET;
  lv_obj_t *arc = lv_arc_create(s_screen);
  lv_obj_set_size(arc, diameter, diameter);
  lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, center - diameter / 2);
  lv_arc_set_rotation(arc, SCAN_ROTATION);
  lv_arc_set_bg_angles(arc, 0, SCAN_FULL_CIRCLE);
  lv_arc_set_range(arc, 0, SCAN_MAX_SCORE);
  lv_arc_set_value(arc, record.score);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(arc, SCAN_RING_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(SCAN_TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, SCAN_RING_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(SCAN_CYAN), LV_PART_INDICATOR);
  snprintf(text, sizeof(text), "%u%%", record.score);
  lv_obj_t *value =
      add_text(text, center - SCAN_PERCENT_Y_OFFSET, SCAN_WHITE, &lv_font_montserrat_16);
  lv_obj_set_width(value, SCAN_PERCENT_WIDTH);
  lv_obj_align(value, LV_ALIGN_TOP_MID, SCAN_PERCENT_X_OFFSET, center - SCAN_PERCENT_Y_OFFSET);
  lv_obj_set_style_transform_scale(value, SCAN_FONT_SCALE, 0);
  int32_t y = center + diameter / 2 + SCAN_DATA_GAP;
  snprintf(text,
           sizeof(text),
           "MAC %02X:%02X:%02X:%02X:%02X:%02X\n#38BDF8 OUI %02X:%02X:%02X#\n"
           "#FBBF24 %.19s#\n#4ADE80 RSSI %d dBm#",
           record.mac[0],
           record.mac[1],
           record.mac[2],
           record.mac[3],
           record.mac[4],
           record.mac[5],
           record.mac[0],
           record.mac[1],
           record.mac[2],
           record.vendor,
           record.rssi_avg);
  lv_obj_t *details = add_text(text, y, SCAN_WHITE, &lv_font_montserrat_12);
  lv_label_set_recolor(details, true);
}

static void build(void) {
  wifi_scan_view_status_t status;
  wifi_scan_view_get_status(&status);
  s_revision = status.revision;
  s_width = ui_screen_w();
  s_height = ui_screen_h();
  lv_obj_clean(s_screen);
  const char *hint = "< > rede | ^ filtro | BACK";
  if (s_phase == UI_WIFI_ESP_SCAN_RESULT) {
    show_result();
  } else {
    add_text("ESP SCAN", SCAN_TITLE_Y, SCAN_CYAN, &lv_font_montserrat_16);
    if (s_phase == UI_WIFI_ESP_SCAN_SEARCHING) {
      waves_create(s_screen, LV_ALIGN_CENTER, 0, SCAN_WAVES_OFFSET, LV_SYMBOL_WIFI, SCAN_ICON);
      add_text(s_is_cancelling ? "Cancelando" : "Buscando...",
               s_height / 2 + SCAN_CAPTION_OFFSET,
               SCAN_WHITE,
               &lv_font_montserrat_14);
      hint = "BACK cancelar";
    } else {
      add_text(status.error != ESP_OK ? "Falha"
                                      : (s_only_priority ? "Filtro: ESP" : "Filtro: Todos"),
               s_height / 2 + SCAN_READY_TITLE_OFFSET,
               SCAN_DIM,
               &lv_font_montserrat_12);
      button_ui_t button = button_ui_create(s_screen,
                                            s_width - SCAN_BUTTON_WIDTH_MARGIN,
                                            SCAN_BUTTON_HEIGHT,
                                            "INICIAR SCAN",
                                            NULL,
                                            NULL);
      lv_obj_set_style_bg_color(button.obj, lv_color_black(), 0);
      lv_obj_set_style_border_color(button.obj, lv_color_hex(SCAN_CYAN), 0);
      lv_obj_set_style_text_color(button.label, lv_color_hex(SCAN_WHITE), 0);
      lv_obj_align(button.obj, LV_ALIGN_CENTER, 0, 0);
      hint = "OK scan | ^ filtro | BACK";
    }
  }
  add_text(hint, s_height - SCAN_FOOTER_OFFSET, SCAN_DIM, &lv_font_montserrat_12);
}

static void start_scan(void) {
  if (wifi_scan_view_start(SPI_WIFI_QUICK_DEFAULT_SECONDS) == ESP_OK) {
    s_phase = UI_WIFI_ESP_SCAN_SEARCHING;
    s_is_cancelling = false;
  }
  build();
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  wifi_scan_view_status_t status;
  wifi_scan_view_get_status(&status);
  if (status.revision != s_revision) {
    if (s_phase == UI_WIFI_ESP_SCAN_SEARCHING && !status.is_busy) {
      collect_results();
      s_phase = status.error == ESP_OK && !s_is_cancelling ? UI_WIFI_ESP_SCAN_RESULT
                                                           : UI_WIFI_ESP_SCAN_READY;
    }
    build();
  }
}

static void on_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool can_navigate = is_press || event->action == INPUT_ACTION_REPEAT;
  if (event->button == INPUT_BTN_BACK && is_press) {
    if (s_phase == UI_WIFI_ESP_SCAN_READY || s_is_cancelling) {
      ui_switch_screen(SCREEN_WIFI_MENU);
      return;
    } else if (s_phase == UI_WIFI_ESP_SCAN_SEARCHING) {
      wifi_scan_view_cancel();
      s_is_cancelling = true;
    } else
      s_phase = UI_WIFI_ESP_SCAN_READY;
  } else if (s_phase != UI_WIFI_ESP_SCAN_SEARCHING) {
    if (event->button == INPUT_BTN_UP && is_press) {
      s_only_priority = !s_only_priority;
      collect_results();
    } else if ((event->button == INPUT_BTN_RIGHT || event->button == INPUT_BTN_DOWN ||
                event->button == INPUT_BTN_LEFT) &&
               can_navigate && s_count != 0) {
      s_index = (s_index + s_count + (event->button == INPUT_BTN_LEFT ? -1 : 1)) % s_count;
    } else if (event->button == INPUT_BTN_OK && is_press && s_phase == UI_WIFI_ESP_SCAN_READY) {
      start_scan();
      return;
    }
  }
  if (is_press || can_navigate)
    build();
}

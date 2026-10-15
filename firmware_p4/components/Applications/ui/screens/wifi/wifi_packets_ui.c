// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_packets_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sys_prio.h"

#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "WIFI_PACKETS_UI";

/* RetroFM-inspired segmented motion, driven here by real C5 Wi-Fi telemetry. */
#define PACKETS_POLL_MS    250
#define RATE_WINDOW_MS     500
#define TASK_STACK_SIZE    8192
#define TASK_PRIORITY      SYS_PRIO_SERVICE_LO
#define SPECTRUM_MIN_SCALE 16U
#define SPECTRUM_SEGMENTS  16U
#define SPECTRUM_HOLD_MS   120U

#define COLOR_BLACK       0x000000
#define COLOR_PANEL       0x000000
#define COLOR_FRAME       0x521477
#define COLOR_GRID        0x210B31
#define COLOR_GRID_BRIGHT 0x481063
#define COLOR_TEXT        0xF1E7FF
#define COLOR_TEXT_MUTED  0x9B7CB5
#define COLOR_PURPLE_LOW  0x532090
#define COLOR_PURPLE_MID  0x8B2FCE
#define COLOR_PURPLE_HIGH 0xD946EF
#define COLOR_PEAK        0xE9D5FF
#define COLOR_HOLD        0xFF62D4
#define COLOR_ACTIVE      0xC084FC
#define COLOR_ERROR       0xFF4778
#define COLOR_WARNING     0xFFB84D

typedef struct {
  const char *name;
  wifi_sniffer_type_t type;
  wifi_analysis_band_t band;
  bool monitor_only;
} capture_def_t;

static const capture_def_t MODES[] = {
    {"LIVE SPECTRUM  DUAL", WIFI_SNIFFER_TYPE_RAW, WIFI_ANALYSIS_BAND_DUAL, true},
    {"LIVE SPECTRUM  2.4G", WIFI_SNIFFER_TYPE_RAW, WIFI_ANALYSIS_BAND_2G, true},
    {"LIVE SPECTRUM  5G", WIFI_SNIFFER_TYPE_RAW, WIFI_ANALYSIS_BAND_5G, true},
    {"RAW PCAP + SPECTRUM", WIFI_SNIFFER_TYPE_RAW, WIFI_ANALYSIS_BAND_DUAL, false},
    {"BEACON PCAP + SCOPE", WIFI_SNIFFER_TYPE_BEACON, WIFI_ANALYSIS_BAND_DUAL, false},
    {"EAPOL WATCH + SCOPE", WIFI_SNIFFER_TYPE_EAPOL, WIFI_ANALYSIS_BAND_DUAL, false},
};
#define MODES_COUNT (sizeof(MODES) / sizeof(MODES[0]))

static const char *const MODE_ICONS[] = {
    "/assets/icons/graphic_eq.bin",
    "/assets/icons/monitoring.bin",
    "/assets/icons/cell_tower.bin",
    "/assets/icons/download.bin",
    "/assets/icons/wifi_find.bin",
    "/assets/icons/key.bin",
};

typedef enum { VIEW_LIST, VIEW_LIVE } view_t;

static lv_obj_t *s_screen;
static menu_component_t s_menu;
static view_t s_view;
static int s_mode_idx;

static lv_obj_t *s_status_label;
static lv_obj_t *s_total_label;
static lv_obj_t *s_types_label;
static lv_obj_t *s_detail_label;
static lv_obj_t *s_axis_label;
static lv_obj_t *s_scale_label;
static lv_obj_t *s_plot;

static uint8_t s_spectrum_value[WIFI_SPECTRUM_BIN_COUNT];
static uint8_t s_spectrum_peak[WIFI_SPECTRUM_BIN_COUNT];
static uint8_t s_spectrum_hold[WIFI_SPECTRUM_BIN_COUNT];
static uint32_t s_spectrum_hold_until[WIFI_SPECTRUM_BIN_COUNT];
static uint32_t s_bin_rate[WIFI_SPECTRUM_BIN_COUNT];
static uint32_t s_bin_prev[WIFI_SPECTRUM_BIN_COUNT];
static int s_visible_start;
static int s_visible_count;
static int s_active_bin = -1;
static uint32_t s_spectrum_scale = SPECTRUM_MIN_SCALE;
static uint32_t s_scale_hold_until;
static bool s_bin_prev_valid;

static volatile bool s_poll_run;
static volatile bool s_worker_busy;
static volatile bool s_capture_started;
static wifi_sniffer_stats_t s_shared_stats;
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_rate_prev_count;
static uint32_t s_rate_prev_ms;
static uint32_t s_last_rate;

static const char *band_name(wifi_analysis_band_t band) {
  if (band == WIFI_ANALYSIS_BAND_2G)
    return "2.4G";
  if (band == WIFI_ANALYSIS_BAND_5G)
    return "5G";
  return "DUAL";
}

static const char *path_basename(const char *path) {
  if (path == NULL)
    return "";
  const char *slash = strrchr(path, '/');
  return slash != NULL ? slash + 1 : path;
}

static int bin_for_channel(uint8_t channel) {
  if (channel >= 1 && channel <= 14)
    return channel - 1;
  if (channel >= 36 && channel <= 64 && ((channel - 36) % 4) == 0)
    return 14 + (channel - 36) / 4;
  if (channel >= 100 && channel <= 144 && ((channel - 100) % 4) == 0)
    return 22 + (channel - 100) / 4;
  if (channel >= 149 && channel <= 165 && ((channel - 149) % 4) == 0)
    return 34 + (channel - 149) / 4;
  return -1;
}

static lv_obj_t *make_label(
    lv_obj_t *parent, int y, const lv_font_t *font, lv_color_t color, lv_text_align_t align) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, ui_screen_w() - 16);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(label, align, 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

static lv_obj_t *make_rule(lv_obj_t *parent, int y, lv_color_t color) {
  lv_obj_t *rule = lv_obj_create(parent);
  lv_obj_remove_style_all(rule);
  lv_obj_set_pos(rule, 0, y);
  lv_obj_set_size(rule, ui_screen_w(), 1);
  lv_obj_set_style_bg_color(rule, color, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  return rule;
}

static void draw_rect(lv_layer_t *layer, int x, int y, int width, int height, lv_color_t color) {
  if (width <= 0 || height <= 0)
    return;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = 0;
  lv_area_t area = {.x1 = x, .y1 = y, .x2 = x + width - 1, .y2 = y + height - 1};
  lv_draw_rect(layer, &dsc, &area);
}

static uint8_t
approach_level(uint8_t previous, uint8_t target, unsigned rise_q8, unsigned fall_q8) {
  unsigned amount;
  if (target == previous)
    return previous;
  if (target > previous) {
    unsigned difference = (unsigned)target - previous;
    amount = (difference * rise_q8 + 128U) >> 8U;
    if (amount == 0U)
      amount = 1U;
    if (amount > difference)
      amount = difference;
    return (uint8_t)(previous + amount);
  }
  unsigned difference = (unsigned)previous - target;
  amount = (difference * fall_q8 + 128U) >> 8U;
  if (amount == 0U)
    amount = 1U;
  if (amount > difference)
    amount = difference;
  return (uint8_t)(previous - amount);
}

static bool time_before(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(deadline_ms - now_ms) > 0;
}

static uint8_t decay_90_percent(uint8_t value) {
  return (uint8_t)(((unsigned)value * 230U + 128U) >> 8U);
}

/* Display-only square-root gamma, matching RetroFM's visual response. */
static uint8_t spectrum_display_level(uint8_t magnitude) {
  uint32_t radicand = (uint32_t)magnitude * 255U;
  uint16_t low = 0;
  uint16_t high = UINT8_MAX;
  while (low < high) {
    uint16_t middle = (uint16_t)((low + high + 1U) >> 1U);
    if ((uint32_t)middle * middle <= radicand)
      low = middle;
    else
      high = (uint16_t)(middle - 1U);
  }
  return (uint8_t)low;
}

static lv_color_t spectrum_segment_color(unsigned segment) {
  if (segment < 6U)
    return lv_color_hex(COLOR_PURPLE_LOW);
  if (segment < 11U)
    return lv_color_hex(COLOR_PURPLE_MID);
  return lv_color_hex(COLOR_PURPLE_HIGH);
}

static void spectrum_draw_event(lv_event_t *event) {
  lv_obj_t *plot = lv_event_get_target_obj(event);
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_area_t coords;
  lv_obj_get_coords(plot, &coords);

  const int width = lv_area_get_width(&coords);
  const int height = lv_area_get_height(&coords);
  const int inner_width = width - 10;
  int pitch = inner_width / s_visible_count;
  if (pitch < 5)
    pitch = 5;
  int bar_width = pitch - 2;
  if (bar_width > 8)
    bar_width = 8;
  if (bar_width < 3)
    bar_width = 3;
  const int used_width = (s_visible_count - 1) * pitch + bar_width;
  const int left = coords.x1 + (width - used_width) / 2;
  const int base_y = coords.y2 - 6;
  const int segment_pitch = (height - 14) / SPECTRUM_SEGMENTS;

  draw_rect(layer, coords.x1, coords.y1, width, height, lv_color_hex(COLOR_PANEL));
  draw_rect(layer, coords.x1, coords.y1, width, 1, lv_color_hex(COLOR_FRAME));
  draw_rect(layer, coords.x1, coords.y2, width, 1, lv_color_hex(COLOR_FRAME));

  for (int marker = 0; marker <= s_visible_count; marker += 4) {
    int x = left + marker * pitch - 1;
    if (x > coords.x1 && x < coords.x2)
      draw_rect(layer, x, coords.y1 + 2, 1, height - 4, lv_color_hex(COLOR_GRID));
  }
  for (int row = 4; row <= 16; row += 4) {
    int y = base_y + 1 - row * segment_pitch;
    draw_rect(layer, coords.x1 + 2, y, width - 4, 1, lv_color_hex(COLOR_GRID));
  }
  if (MODES[s_mode_idx].band == WIFI_ANALYSIS_BAND_DUAL) {
    int divider_x = left + 14 * pitch - 1;
    draw_rect(layer, divider_x, coords.y1 + 2, 1, height - 4, lv_color_hex(COLOR_GRID_BRIGHT));
  }

  for (int visible = 0; visible < s_visible_count; ++visible) {
    const int bin = s_visible_start + visible;
    const int x = left + visible * pitch;
    const unsigned filled = ((unsigned)s_spectrum_value[bin] * SPECTRUM_SEGMENTS + 254U) / 255U;
    const unsigned peak = ((unsigned)s_spectrum_peak[bin] * SPECTRUM_SEGMENTS) / 255U;
    const unsigned hold = ((unsigned)s_spectrum_hold[bin] * SPECTRUM_SEGMENTS) / 255U;
    for (unsigned segment = 0; segment < SPECTRUM_SEGMENTS; ++segment) {
      if (segment < filled) {
        const int y = base_y - (int)segment * segment_pitch - (segment_pitch - 2);
        draw_rect(layer, x, y, bar_width, segment_pitch - 1, spectrum_segment_color(segment));
      }
    }
    if (peak != 0U) {
      const int y = base_y + 1 - (int)peak * segment_pitch;
      draw_rect(layer, x, y, bar_width, 1, lv_color_hex(COLOR_PEAK));
    }
    if (hold != 0U) {
      const int y = base_y + 1 - (int)hold * segment_pitch;
      draw_rect(layer, x, y, bar_width, 1, lv_color_hex(COLOR_HOLD));
    }
    if (bin == s_active_bin)
      draw_rect(layer, x, coords.y2 - 3, bar_width, 2, lv_color_hex(COLOR_ACTIVE));
  }
}

static void clear_live_refs(void) {
  s_status_label = NULL;
  s_total_label = NULL;
  s_types_label = NULL;
  s_detail_label = NULL;
  s_axis_label = NULL;
  s_scale_label = NULL;
  s_plot = NULL;
}

static void reset_spectrum_state(void) {
  memset(s_spectrum_value, 0, sizeof(s_spectrum_value));
  memset(s_spectrum_peak, 0, sizeof(s_spectrum_peak));
  memset(s_spectrum_hold, 0, sizeof(s_spectrum_hold));
  memset(s_spectrum_hold_until, 0, sizeof(s_spectrum_hold_until));
  memset(s_bin_rate, 0, sizeof(s_bin_rate));
  memset(s_bin_prev, 0, sizeof(s_bin_prev));
  s_spectrum_scale = SPECTRUM_MIN_SCALE;
  s_scale_hold_until = 0;
  s_bin_prev_valid = false;
  s_active_bin = -1;
}

static void build_list_view(void) {
  lv_obj_clean(s_screen);
  clear_live_refs();
  s_menu = menu_component_create(s_screen, "PACKET LAB", "/assets/icons/graphic_eq.bin");
  for (size_t i = 0; i < MODES_COUNT; ++i)
    menu_component_add_item(&s_menu, MODE_ICONS[i], MODES[i].name);
  menu_component_set_hint(&s_menu, "Live RF spectrum • passive • PCAP to SD");
  s_view = VIEW_LIST;
}

static void configure_visible_bins(wifi_analysis_band_t band) {
  if (band == WIFI_ANALYSIS_BAND_2G) {
    s_visible_start = 0;
    s_visible_count = 14;
  } else if (band == WIFI_ANALYSIS_BAND_5G) {
    s_visible_start = 14;
    s_visible_count = 25;
  } else {
    s_visible_start = 0;
    s_visible_count = WIFI_SPECTRUM_BIN_COUNT;
  }
}

static void build_spectrum_plot(wifi_analysis_band_t band) {
  configure_visible_bins(band);
  s_plot = lv_obj_create(s_screen);
  lv_obj_remove_style_all(s_plot);
  lv_obj_set_size(s_plot, ui_screen_w() - 8, ui_screen_h() - 135);
  lv_obj_align(s_plot, LV_ALIGN_TOP_MID, 0, 43);
  lv_obj_remove_flag(s_plot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_plot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_plot, spectrum_draw_event, LV_EVENT_DRAW_MAIN, NULL);
}

static void build_live_view(int idx) {
  lv_obj_clean(s_screen);
  clear_live_refs();
  reset_spectrum_state();
  s_mode_idx = idx;
  const capture_def_t *mode = &MODES[idx];
  const int plot_bottom = ui_screen_h() - 92;

  lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BLACK), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_t *header_left =
      make_label(s_screen, 5, &lv_font_montserrat_12, lv_color_hex(COLOR_TEXT), LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(header_left, "HIGHBOY");
  lv_obj_t *header_right = make_label(
      s_screen, 5, &lv_font_montserrat_12, lv_color_hex(COLOR_HOLD), LV_TEXT_ALIGN_RIGHT);
  lv_label_set_text(header_right, "RF LIVE");
  make_rule(s_screen, 21, lv_color_hex(COLOR_FRAME));

  s_status_label = make_label(
      s_screen, 25, &lv_font_montserrat_12, lv_color_hex(COLOR_TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
  lv_label_set_text_fmt(s_status_label, "%s  •  HOPPING", band_name(mode->band));
  build_spectrum_plot(mode->band);

  s_axis_label = make_label(s_screen,
                            plot_bottom + 2,
                            &lv_font_montserrat_12,
                            lv_color_hex(COLOR_TEXT_MUTED),
                            LV_TEXT_ALIGN_CENTER);
  if (mode->band == WIFI_ANALYSIS_BAND_2G)
    lv_label_set_text(s_axis_label, "CH  1     3     6     9     11    14");
  else if (mode->band == WIFI_ANALYSIS_BAND_5G)
    lv_label_set_text(s_axis_label, "36   64 | 100   144 | 149   165");
  else
    lv_label_set_text(s_axis_label, "1   14 | 36   64   100   144   165");

  s_total_label = make_label(s_screen,
                             plot_bottom + 19,
                             &lv_font_montserrat_16,
                             lv_color_hex(COLOR_TEXT),
                             LV_TEXT_ALIGN_CENTER);
  s_types_label = make_label(s_screen,
                             plot_bottom + 39,
                             &lv_font_montserrat_12,
                             lv_color_hex(COLOR_ACTIVE),
                             LV_TEXT_ALIGN_CENTER);
  s_detail_label = make_label(s_screen,
                              plot_bottom + 56,
                              &lv_font_montserrat_12,
                              lv_color_hex(COLOR_TEXT_MUTED),
                              LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(s_total_label, "STARTING C5 RADIO...");
  lv_label_set_text(s_types_label, "M 0   D 0   C 0   AP 0");
  lv_label_set_text(s_detail_label,
                    mode->monitor_only ? "PASSIVE • PACKET ACTIVITY" : "OPENING SD PCAP...");

  make_rule(s_screen, ui_screen_h() - 18, lv_color_hex(COLOR_GRID_BRIGHT));
  s_scale_label = make_label(s_screen,
                             ui_screen_h() - 15,
                             &lv_font_montserrat_12,
                             lv_color_hex(COLOR_TEXT_MUTED),
                             LV_TEXT_ALIGN_LEFT);
  lv_label_set_text_fmt(s_scale_label, "MAX %u/s", (unsigned)s_spectrum_scale);
  lv_obj_t *footer = make_label(s_screen,
                                ui_screen_h() - 15,
                                &lv_font_montserrat_12,
                                lv_color_hex(COLOR_TEXT_MUTED),
                                LV_TEXT_ALIGN_RIGHT);
  lv_label_set_text(footer, mode->monitor_only ? "BACK  STOP" : "BACK  STOP + SAVE");
  s_view = VIEW_LIVE;
}

static void update_rate_targets(const wifi_sniffer_stats_t *st, uint32_t dt_ms, uint32_t now_ms) {
  uint32_t max_rate = 0;
  for (int i = 0; i < WIFI_SPECTRUM_BIN_COUNT; ++i) {
    const uint32_t count = st->channel_packets[i];
    const uint32_t delta = s_bin_prev_valid ? count - s_bin_prev[i] : 0;
    s_bin_prev[i] = count;
    s_bin_rate[i] = dt_ms > 0 ? (uint32_t)(((uint64_t)delta * 1000ULL) / dt_ms) : 0;
    if (i >= s_visible_start && i < s_visible_start + s_visible_count && s_bin_rate[i] > max_rate)
      max_rate = s_bin_rate[i];
  }
  s_bin_prev_valid = true;
  if (max_rate > s_spectrum_scale) {
    while (s_spectrum_scale < max_rate && s_spectrum_scale < (1U << 30))
      s_spectrum_scale *= 2U;
    s_scale_hold_until = now_ms + 6000U;
  } else if (!time_before(now_ms, s_scale_hold_until) && s_spectrum_scale > SPECTRUM_MIN_SCALE &&
             max_rate < s_spectrum_scale / 4U) {
    s_spectrum_scale /= 2U;
    if (s_spectrum_scale < SPECTRUM_MIN_SCALE)
      s_spectrum_scale = SPECTRUM_MIN_SCALE;
    s_scale_hold_until = now_ms + 3000U;
  }
}

static void update_visual_levels(uint32_t now_ms) {
  for (int i = 0; i < WIFI_SPECTRUM_BIN_COUNT; ++i) {
    uint32_t scaled = s_spectrum_scale > 0
                          ? (uint32_t)(((uint64_t)s_bin_rate[i] * 255ULL) / s_spectrum_scale)
                          : 0;
    if (scaled > 255U)
      scaled = 255U;
    if (s_bin_rate[i] > 0 && scaled == 0)
      scaled = 1U;
    const uint8_t target = spectrum_display_level((uint8_t)scaled);
    const uint8_t value = approach_level(s_spectrum_value[i], target, 179U, 64U);
    s_spectrum_value[i] = value;
    s_spectrum_peak[i] = approach_level(s_spectrum_peak[i], value, 154U, 64U);
    if (s_spectrum_peak[i] >= s_spectrum_hold[i]) {
      s_spectrum_hold[i] = s_spectrum_peak[i];
      s_spectrum_hold_until[i] = now_ms + SPECTRUM_HOLD_MS;
    } else if (!time_before(now_ms, s_spectrum_hold_until[i])) {
      s_spectrum_hold[i] = decay_90_percent(s_spectrum_hold[i]);
    }
  }
  if (s_plot != NULL)
    lv_obj_invalidate(s_plot);
}

static void live_update_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_PACKETS_MENU || s_view != VIEW_LIVE) {
    s_poll_run = false;
    return;
  }
  wifi_sniffer_stats_t st;
  portENTER_CRITICAL(&s_stats_mux);
  st = s_shared_stats;
  const bool started = s_capture_started;
  portEXIT_CRITICAL(&s_stats_mux);
  if (!started) {
    lv_label_set_text(s_total_label, "C5 START FAILED");
    lv_obj_set_style_text_color(s_total_label, lv_color_hex(COLOR_ERROR), 0);
    return;
  }

  const uint32_t now = lv_tick_get();
  uint32_t rate = s_last_rate;
  if (s_rate_prev_ms == 0) {
    s_rate_prev_ms = now;
    s_rate_prev_count = st.packets;
    memcpy(s_bin_prev, st.channel_packets, sizeof(s_bin_prev));
    s_bin_prev_valid = true;
  } else {
    const uint32_t dt = now - s_rate_prev_ms;
    if (dt >= RATE_WINDOW_MS) {
      rate = (uint32_t)(((uint64_t)(st.packets - s_rate_prev_count) * 1000ULL) / dt);
      s_last_rate = rate;
      s_rate_prev_ms = now;
      s_rate_prev_count = st.packets;
      update_rate_targets(&st, dt, now);
    }
  }

  s_active_bin = bin_for_channel(st.channel);
  update_visual_levels(now);
  lv_label_set_text_fmt(s_status_label,
                        "%s  •  CH %u  •  %d dBm",
                        band_name(MODES[s_mode_idx].band),
                        st.channel,
                        st.last_rssi);
  lv_label_set_text_fmt(s_scale_label, "MAX %lu/s", (unsigned long)s_spectrum_scale);
  lv_label_set_text_fmt(
      s_total_label, "%08lu PKT   %05lu/s", (unsigned long)st.packets, (unsigned long)rate);
  lv_label_set_text_fmt(s_types_label,
                        "M %lu   D %lu   C %lu   AP %lu",
                        (unsigned long)st.mgmt_frames,
                        (unsigned long)st.data_frames,
                        (unsigned long)st.ctrl_frames,
                        (unsigned long)st.unique_aps);

  if (st.deauths > 0) {
    lv_label_set_text_fmt(s_detail_label,
                          "DEAUTH OBS %lu  •  2G %lu / 5G %lu",
                          (unsigned long)st.deauths,
                          (unsigned long)st.pkts_2ghz,
                          (unsigned long)st.pkts_5ghz);
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_WARNING), 0);
  } else if (!MODES[s_mode_idx].monitor_only) {
    lv_label_set_text_fmt(s_detail_label,
                          "PCAP %lu KB  •  %s",
                          (unsigned long)(wifi_sniffer_get_capture_size() / 1024),
                          path_basename(wifi_sniffer_get_capture_path()));
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_HOLD), 0);
  } else {
    lv_label_set_text_fmt(s_detail_label,
                          "2G %lu  •  5G %lu  •  PROBE %lu",
                          (unsigned long)st.pkts_2ghz,
                          (unsigned long)st.pkts_5ghz,
                          (unsigned long)st.probe_reqs);
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_TEXT_MUTED), 0);
  }
}

static void packet_worker(void *arg) {
  (void)arg;
  const capture_def_t mode = MODES[s_mode_idx];
  ESP_LOGI(TAG, "Starting passive packet monitor: %s", mode.name);
  wifi_service_start();
  ESP_LOGI(TAG, "Wi-Fi start returned; opening packet session");
  const bool ok = mode.monitor_only ? wifi_sniffer_start_monitor_band(0, mode.band)
                                    : wifi_sniffer_start_band(mode.type, 0, mode.band);
  ESP_LOGI(TAG, "Packet session start: %s", ok ? "OK" : "FAILED");
  portENTER_CRITICAL(&s_stats_mux);
  s_capture_started = ok;
  memset(&s_shared_stats, 0, sizeof(s_shared_stats));
  portEXIT_CRITICAL(&s_stats_mux);
  ui_async_call(live_update_cb, NULL);
  while (ok && s_poll_run) {
    wifi_sniffer_stats_t st;
    if (wifi_sniffer_get_stats(&st)) {
      portENTER_CRITICAL(&s_stats_mux);
      s_shared_stats = st;
      portEXIT_CRITICAL(&s_stats_mux);
      ui_async_call(live_update_cb, NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(PACKETS_POLL_MS));
  }
  if (ok)
    wifi_sniffer_stop();
  s_worker_busy = false;
  vTaskDelete(NULL);
}

static void start_capture(void) {
  s_rate_prev_count = 0;
  s_rate_prev_ms = 0;
  s_last_rate = 0;
  s_poll_run = true;
  s_capture_started = false;
  if (s_worker_busy)
    return;
  s_worker_busy = true;
  if (xTaskCreatePinnedToCore(packet_worker,
                              "packet_lab",
                              TASK_STACK_SIZE,
                              NULL,
                              TASK_PRIORITY,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    s_worker_busy = false;
    s_poll_run = false;
    ESP_LOGE(TAG, "Failed to create packet monitor task");
    lv_label_set_text(s_total_label, "MONITOR TASK FAILED");
    lv_obj_set_style_text_color(s_total_label, lv_color_hex(COLOR_ERROR), 0);
  }
}

static void wifi_packets_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = ev->action == INPUT_ACTION_PRESS;
  const bool nav = press || ev->action == INPUT_ACTION_REPEAT;
  if (s_view == VIEW_LIST) {
    if (ev->button == INPUT_BTN_DOWN && nav)
      menu_component_next(&s_menu);
    else if (ev->button == INPUT_BTN_UP && nav)
      menu_component_prev(&s_menu);
    else if (ev->button == INPUT_BTN_BACK && press)
      ui_switch_screen(SCREEN_WIFI_MENU);
    else if ((ev->button == INPUT_BTN_OK || ev->button == INPUT_BTN_RIGHT) && press &&
             !s_worker_busy) {
      const int selected = menu_component_get_selected(&s_menu);
      if (selected >= 0 && selected < (int)MODES_COUNT) {
        build_live_view(selected);
        start_capture();
      }
    }
  } else if (ev->button == INPUT_BTN_BACK && press) {
    s_poll_run = false;
    build_list_view();
  }
}

void ui_wifi_packets_open(void) {
  s_poll_run = false;
  if (s_screen != NULL)
    lv_obj_del(s_screen);
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BLACK), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  build_list_view();
  ui_input_set_screen_handler(wifi_packets_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

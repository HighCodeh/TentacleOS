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

#include "wifi_packets_ui.h"

#include "esp_log.h"
#include "st7789.h"

#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "WIFI_PACKETS_UI";

#define CAPTURE_TICK_MS 120

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2

typedef struct {
  const char *name;
  int step;
  bool eapol;
  int channel;
} capture_def_t;

static const capture_def_t MODES[] = {
    {"Raw Sniffer", 9, false, 6},
    {"EAPOL / Handshake", 4, true, 1},
    {"Beacon Sniff", 6, false, 11},
    {"PMKID", 3, false, 3},
};
#define MODES_COUNT (sizeof(MODES) / sizeof(MODES[0]))

static const char *const MODE_ICONS[] = {
    "/assets/icons/monitoring.bin",
    "/assets/icons/key.bin",
    "/assets/icons/cell_tower.bin",
    "/assets/icons/vpn_key.bin",
};

typedef enum {
  VIEW_LIST,
  VIEW_CAPTURING,
} view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_capture_timer = NULL;

static view_t s_view = VIEW_LIST;
static int s_mode_idx = 0;
static long s_packets = 0;
static long s_packets_prev = 0;
static int s_tick_accum = 0;
static int s_handshakes = 0;
static int s_capture_seq = 0;
static lv_obj_t *s_packet_label = NULL;
static lv_obj_t *s_rate_label = NULL;
static lv_obj_t *s_handshake_label = NULL;
static lv_obj_t *s_hex_label = NULL;
static lv_obj_t *s_waves = NULL;
static uint32_t s_hex_seed = 0xC0FFEE11;

static void wifi_packets_input(const input_event_t *ev, void *ctx);
static void capture_tick_cb(lv_timer_t *timer);
static void build_list_view(void);
static void build_capturing_view(int idx);
static void stop_capture_timer(void);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static uint32_t next_rand(void) {
  s_hex_seed = s_hex_seed * 1664525u + 1013904223u;
  return s_hex_seed;
}

static void make_hex_line(char *buf, size_t buflen) {
  size_t pos = 0;
  for (int i = 0; i < 6 && pos + 3 < buflen; i++) {
    int n = snprintf(
        buf + pos, buflen - pos, (i == 0) ? "%02X" : " %02X", (unsigned)(next_rand() & 0xFF));
    if (n < 0)
      break;
    pos += (size_t)n;
  }
}

void ui_wifi_packets_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_capture_timer = NULL;
  s_view = VIEW_LIST;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  build_list_view();

  ui_input_set_screen_handler(wifi_packets_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void clear_screen_children(void) {
  lv_obj_clean(s_screen);
  s_packet_label = NULL;
  s_rate_label = NULL;
  s_handshake_label = NULL;
  s_hex_label = NULL;
  s_waves = NULL;
}

static void build_list_view(void) {
  clear_screen_children();
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  s_menu = menu_component_create(s_screen, "PACKETS", "/assets/icons/download.bin");
  for (size_t i = 0; i < MODES_COUNT; i++)
    menu_component_add_item(&s_menu, MODE_ICONS[i], MODES[i].name);

  fade_in(s_menu.title_bar, 200);
  fade_in(s_menu.items_cont, 200);

  s_view = VIEW_LIST;
}

static void build_capturing_view(int idx) {
  stop_capture_timer();
  clear_screen_children();

  s_mode_idx = idx;
  s_packets = 0;
  s_packets_prev = 0;
  s_tick_accum = 0;
  s_handshakes = 0;

  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *header = ui_chrome_header(s_screen, MODES[idx].name, "/assets/icons/download.bin");

  lv_obj_t *status_label = lv_label_create(s_screen);
  lv_label_set_text(status_label, "Capturing...");
  lv_obj_set_style_text_color(status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 8);

  lv_obj_t *chan_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(chan_label, "CH %d", MODES[idx].channel);
  lv_obj_set_style_text_color(chan_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(chan_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(chan_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(chan_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 30);

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, 12, LV_SYMBOL_DOWNLOAD, NULL);

  int packet_y = -56;
  if (MODES[idx].eapol) {
    s_handshake_label = lv_label_create(s_screen);
    lv_label_set_text_fmt(s_handshake_label, "Handshakes: %d", s_handshakes);
    lv_obj_set_style_text_color(s_handshake_label, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(s_handshake_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_handshake_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_handshake_label, LV_ALIGN_BOTTOM_MID, 0, -56);
    packet_y = -76;
  }

  s_packet_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_packet_label, "Packets: %ld", s_packets);
  lv_obj_set_style_text_color(s_packet_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_packet_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_packet_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_packet_label, LV_ALIGN_BOTTOM_MID, 0, packet_y);

  s_rate_label = lv_label_create(s_screen);
  lv_label_set_text(s_rate_label, "0 pkt/s");
  lv_obj_set_style_text_color(s_rate_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_rate_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_rate_label, LV_ALIGN_BOTTOM_MID, 0, -42);

  s_hex_label = lv_label_create(s_screen);
  lv_label_set_text(s_hex_label, "-- -- -- -- -- --");
  lv_obj_set_style_text_color(s_hex_label, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(s_hex_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_hex_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_hex_label, LV_ALIGN_BOTTOM_MID, 0, -28);

  ui_chrome_footer(s_screen, "BACK to stop & save");

  fade_in(header, 200);
  fade_in(status_label, 200);
  fade_in(chan_label, 200);
  fade_in(s_packet_label, 200);
  if (s_handshake_label != NULL)
    fade_in(s_handshake_label, 200);

  s_view = VIEW_CAPTURING;
  s_capture_timer = lv_timer_create(capture_tick_cb, CAPTURE_TICK_MS, NULL);
}

static void stop_capture_timer(void) {
  if (s_capture_timer != NULL) {
    lv_timer_delete(s_capture_timer);
    s_capture_timer = NULL;
  }
}

static void capture_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_capture_timer == timer)
      s_capture_timer = NULL;
    return;
  }
  if (s_view != VIEW_CAPTURING || s_packet_label == NULL)
    return;

  s_packets += MODES[s_mode_idx].step;
  lv_label_set_text_fmt(s_packet_label, "Packets: %ld", s_packets);

  if (s_hex_label != NULL) {
    char hex[24];
    make_hex_line(hex, sizeof(hex));
    lv_label_set_text(s_hex_label, hex);
  }

  s_tick_accum++;
  int ticks_per_sec = (1000 + CAPTURE_TICK_MS - 1) / CAPTURE_TICK_MS;
  if (s_tick_accum >= ticks_per_sec && s_rate_label != NULL) {
    long delta = s_packets - s_packets_prev;
    long per_sec = delta * 1000 / (s_tick_accum * CAPTURE_TICK_MS);
    lv_label_set_text_fmt(s_rate_label, "%ld pkt/s", per_sec);
    s_packets_prev = s_packets;
    s_tick_accum = 0;
  }

  if (MODES[s_mode_idx].eapol && s_handshake_label != NULL) {
    if ((s_packets / MODES[s_mode_idx].step) % 12 == 0) {
      s_handshakes++;
      lv_label_set_text_fmt(s_handshake_label, "Handshakes: %d", s_handshakes);
    }
  }
}

static void wifi_packets_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_view == VIEW_LIST) {
    switch (ev->button) {
      case INPUT_BTN_DOWN:
        if (nav)
          menu_component_next(&s_menu);
        break;
      case INPUT_BTN_UP:
        if (nav)
          menu_component_prev(&s_menu);
        break;
      case INPUT_BTN_BACK:
      case INPUT_BTN_LEFT:
        if (press)
          ui_switch_screen(SCREEN_WIFI_MENU);
        break;
      case INPUT_BTN_OK:
        if (press) {
          int sel = menu_component_get_selected(&s_menu);
          if (sel >= 0 && sel < (int)MODES_COUNT) {
            ESP_LOGI(TAG, "mock capture start: %s", MODES[sel].name);
            build_capturing_view(sel);
          }
        }
        break;
      default:
        break;
    }
  } else {
    switch (ev->button) {
      case INPUT_BTN_BACK:
      case INPUT_BTN_LEFT:
        if (press) {
          stop_capture_timer();
          s_capture_seq++;
          ESP_LOGI(TAG, "mock capture stopped: %s (%ld pkts)", MODES[s_mode_idx].name, s_packets);
          char msg[48];
          snprintf(msg, sizeof(msg), "Saved capture_%02d.pcap", s_capture_seq);
          notify(NOTIFY_SAVED, msg);
          build_list_view();
        }
        break;
      default:
        break;
    }
  }
}

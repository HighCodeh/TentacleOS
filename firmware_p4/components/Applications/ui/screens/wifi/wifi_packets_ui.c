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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "st7789.h"
#include "sys_prio.h"

#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "WIFI_PACKETS_UI";

#define PACKETS_POLL_MS 500
#define RATE_WINDOW_MS  1000
#define TASK_STACK_SIZE 8192
#define TASK_PRIORITY   SYS_PRIO_SERVICE_LO

#define TOP_BORDER_H 46

#define PACKET_Y_PLAIN (-56)
#define PACKET_Y_EAPOL (-76)
#define HANDSHAKE_Y    (-56)
#define RATE_Y         (-42)

typedef struct {
  const char *name;
  wifi_sniffer_type_t type;
  bool eapol;
  uint8_t channel;
} capture_def_t;

static const capture_def_t MODES[] = {
    {"Raw Sniffer", WIFI_SNIFFER_TYPE_RAW, false, 6},
    {"EAPOL / Handshake", WIFI_SNIFFER_TYPE_EAPOL, true, 1},
    {"Beacon Sniff", WIFI_SNIFFER_TYPE_BEACON, false, 11},
    {"PMKID", WIFI_SNIFFER_TYPE_PMKID, false, 3},
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

static view_t s_view = VIEW_LIST;
static int s_mode_idx = 0;
static lv_obj_t *s_packet_label = NULL;
static lv_obj_t *s_rate_label = NULL;
static lv_obj_t *s_handshake_label = NULL;
static lv_obj_t *s_waves = NULL;

static volatile bool s_poll_run = false;
static volatile bool s_worker_busy = false;
static volatile uint32_t s_pkt_count = 0;
static volatile bool s_hs_captured = false;

static uint32_t s_rate_prev_count = 0;
static uint32_t s_rate_prev_ms = 0;

static void wifi_packets_input(const input_event_t *ev, void *ctx);
static void build_list_view(void);
static void build_capturing_view(int idx);
static void start_capture(void);
static void stop_capture(void);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void packets_update_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_PACKETS_MENU || s_view != VIEW_CAPTURING) {
    s_poll_run = false;
    return;
  }
  if (s_packet_label == NULL)
    return;

  uint32_t count = s_pkt_count;
  lv_label_set_text_fmt(s_packet_label, "Packets: %lu", (unsigned long)count);

  uint32_t now = lv_tick_get();
  if (s_rate_prev_ms == 0) {
    s_rate_prev_ms = now;
    s_rate_prev_count = count;
  } else if (s_rate_label != NULL) {
    uint32_t dt = now - s_rate_prev_ms;
    if (dt >= RATE_WINDOW_MS) {
      unsigned long per_sec = (unsigned long)(count - s_rate_prev_count) * 1000UL / dt;
      lv_label_set_text_fmt(s_rate_label, "%lu pkt/s", per_sec);
      s_rate_prev_ms = now;
      s_rate_prev_count = count;
    }
  }

  if (MODES[s_mode_idx].eapol && s_handshake_label != NULL)
    lv_label_set_text_fmt(s_handshake_label, "Handshakes: %d", s_hs_captured ? 1 : 0);
}

static void packets_worker(void *arg) {
  (void)arg;
  int mode_idx = s_mode_idx;

  wifi_service_start();
  wifi_sniffer_start(MODES[mode_idx].type, MODES[mode_idx].channel);

  while (s_poll_run) {
    uint32_t count = wifi_sniffer_get_packet_count();
    bool hs = false;
    if (MODES[mode_idx].eapol)
      hs = wifi_sniffer_handshake_captured();
    s_pkt_count = count;
    s_hs_captured = hs;
    ui_async_call(packets_update_cb, NULL);
    vTaskDelay(pdMS_TO_TICKS(PACKETS_POLL_MS));
  }

  wifi_sniffer_stop();
  s_worker_busy = false;
  vTaskDelete(NULL);
}

static void start_capture(void) {
  s_pkt_count = 0;
  s_hs_captured = false;
  s_rate_prev_count = 0;
  s_rate_prev_ms = 0;

  s_poll_run = true;
  if (!s_worker_busy) {
    s_worker_busy = true;
    if (xTaskCreatePinnedToCore(packets_worker,
                                "pkts_worker",
                                TASK_STACK_SIZE,
                                NULL,
                                TASK_PRIORITY,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_worker_busy = false;
      s_poll_run = false;
      ESP_LOGW(TAG, "failed to spawn packet capture worker");
    }
  }
}

static void stop_capture(void) {
  s_poll_run = false;
}

void ui_wifi_packets_open(void) {
  stop_capture();

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
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
  stop_capture();
  clear_screen_children();

  s_mode_idx = idx;

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

  int packet_y = PACKET_Y_PLAIN;
  if (MODES[idx].eapol) {
    s_handshake_label = lv_label_create(s_screen);
    lv_label_set_text(s_handshake_label, "Handshakes: 0");
    lv_obj_set_style_text_color(s_handshake_label, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(s_handshake_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_handshake_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_handshake_label, LV_ALIGN_BOTTOM_MID, 0, HANDSHAKE_Y);
    packet_y = PACKET_Y_EAPOL;
  }

  s_packet_label = lv_label_create(s_screen);
  lv_label_set_text(s_packet_label, "Packets: 0");
  lv_obj_set_style_text_color(s_packet_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_packet_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_packet_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_packet_label, LV_ALIGN_BOTTOM_MID, 0, packet_y);

  s_rate_label = lv_label_create(s_screen);
  lv_label_set_text(s_rate_label, "0 pkt/s");
  lv_obj_set_style_text_color(s_rate_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_rate_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_rate_label, LV_ALIGN_BOTTOM_MID, 0, RATE_Y);

  ui_chrome_footer(s_screen, "BACK to stop");

  fade_in(header, 200);
  fade_in(status_label, 200);
  fade_in(chan_label, 200);
  fade_in(s_packet_label, 200);
  if (s_handshake_label != NULL)
    fade_in(s_handshake_label, 200);

  s_view = VIEW_CAPTURING;
  start_capture();
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
            ESP_LOGI(TAG, "capture start: %s", MODES[sel].name);
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
          stop_capture();
          ESP_LOGI(TAG, "capture stopped: %s", MODES[s_mode_idx].name);
          build_list_view();
        }
        break;
      default:
        break;
    }
  }
}

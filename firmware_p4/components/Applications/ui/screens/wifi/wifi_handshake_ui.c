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

#include "wifi_handshake_ui.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "st7789.h"
#include "sys_prio.h"

#include "ui_chrome.h"
#include "menu_component_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"
#include "led_control.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "WIFI_HANDSHAKE_UI";

#define POLL_INTERVAL_MS      1000
#define TASK_STACK_SIZE       8192
#define TASK_PRIORITY         SYS_PRIO_SERVICE_LO
#define MONITOR_CHANNEL       0
#define FLAG_HANDSHAKE        0x1u
#define FLAG_PMKID            0x2u
#define FLAG_FAILED           0x4u
#define FLAG_MASK             0x7u
#define GENERATION_STEP       8u
#define CONTENT_MARGIN        8
#define SCROLL_GUTTER         24
#define CONTENT_GAP           8
#define LOG_BUFFER_SIZE       2048
#define SCROLL_STEP           60
#define CARD_RADIUS           10
#define CARD_PADDING          10
#define CARD_GAP              4
#define COUNTER_WIDTH_PERCENT 48

static menu_component_t s_scroll;
static lv_obj_t *s_screen;
static lv_obj_t *s_body;
static lv_obj_t *s_status;
static lv_obj_t *s_summary;
static lv_obj_t *s_channel_label;
static lv_obj_t *s_saved_count;
static lv_obj_t *s_saved_size;
static lv_obj_t *s_queue_status;
static lv_obj_t *s_pair_badge;
static lv_obj_t *s_pmkid_badge;
static lv_obj_t *s_observed;
static lv_obj_t *s_log;
static atomic_bool s_is_poll_running;
static atomic_uint s_generation;
static atomic_uint s_worker_generation;
static bool s_is_exit_requested;
static atomic_bool s_is_worker_busy;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_hs_bssid[WIFI_AP_CAPTURE_MAC_SIZE];
static uint8_t s_pm_bssid[WIFI_AP_CAPTURE_MAC_SIZE];
static wifi_ap_capture_snapshot_t s_saved;
static uint32_t s_packets;
static uint8_t s_channel;

static uint16_t s_capture_attempts;
static uint16_t s_capture_failures;
static uint8_t s_capture_hold_seconds;
static uint8_t s_capture_ap_attempt;

static void publish_snapshot(const wifi_sniffer_stats_t *stats) {
  wifi_ap_capture_snapshot_t saved = {0};
  wifi_sniffer_capture_snapshot(&saved);
  portENTER_CRITICAL(&s_snapshot_lock);
  s_saved = saved;
  bool has_failure = stats->capture_failures != s_capture_failures && stats->capture_failures != 0;
  bool has_attempt = stats->capture_attempts != s_capture_attempts && stats->capture_attempts != 0;
  s_capture_attempts = stats->capture_attempts;
  s_capture_failures = stats->capture_failures;
  s_capture_hold_seconds = stats->capture_hold_seconds;
  s_capture_ap_attempt = stats->capture_ap_attempt;
  s_packets = stats->packets;
  s_channel = stats->channel;
  portEXIT_CRITICAL(&s_snapshot_lock);
  if (has_failure) {
    ESP_LOGW(TAG, "Lab capture: C5 rejected TX; continuing to listen");
    led_signal_warning();
  } else if (has_attempt) {
    ESP_LOGI(TAG, "Lab capture: C5 accepted TX; listening for EAPOL");
    led_signal_info();
  }
}

static void capture_stopped_cb(void *param) {
  if ((uint32_t)(uintptr_t)param == atomic_load(&s_generation) &&
      ui_current_screen() == SCREEN_WIFI_HANDSHAKE && s_is_exit_requested)
    ui_switch_screen(SCREEN_WIFI_MENU);
}

static void state_ready_cb(void *param) {
  uint32_t update = (uint32_t)(uintptr_t)param;
  if ((update & ~FLAG_MASK) != atomic_load(&s_generation) ||
      ui_current_screen() != SCREEN_WIFI_HANDSHAKE || s_status == NULL || s_is_exit_requested)
    return;
  wifi_ap_capture_snapshot_t saved;
  uint32_t packets;
  uint16_t attempts, failures;
  uint8_t hold_seconds, ap_attempt;
  uint8_t channel;
  uint8_t hs[WIFI_AP_CAPTURE_MAC_SIZE], pm[WIFI_AP_CAPTURE_MAC_SIZE];
  portENTER_CRITICAL(&s_snapshot_lock);
  saved = s_saved;
  attempts = s_capture_attempts;
  failures = s_capture_failures;
  hold_seconds = s_capture_hold_seconds;
  ap_attempt = s_capture_ap_attempt;
  packets = s_packets;
  channel = s_channel;
  memcpy(hs, s_hs_bssid, sizeof(hs));
  memcpy(pm, s_pm_bssid, sizeof(pm));
  portEXIT_CRITICAL(&s_snapshot_lock);
  if (hold_seconds != 0)
    lv_label_set_text_fmt(s_status, "Listening %us (attempt %u/3)", hold_seconds, ap_attempt);
  else
    lv_label_set_text(s_status, "Scanning: all networks");
  lv_label_set_text_fmt(s_channel_label, LV_SYMBOL_WIFI "  CHANNEL %u  /  AUTO", channel);
  lv_label_set_text_fmt(s_summary, "%lu", (unsigned long)packets);
  lv_label_set_text_fmt(s_saved_count, "%lu", (unsigned long)saved.frames);
  lv_label_set_text_fmt(s_saved_size, "%lu bytes on SD", (unsigned long)saved.bytes);
  lv_label_set_text(s_pair_badge, (update & FLAG_HANDSHAKE) ? LV_SYMBOL_OK " PAIR" : "-- PAIR");
  lv_label_set_text(s_pmkid_badge, (update & FLAG_PMKID) ? LV_SYMBOL_OK " PMKID" : "-- PMKID");
  lv_obj_set_style_text_color(s_pair_badge,
                              (update & FLAG_HANDSHAKE) ? current_theme.border_accent
                                                        : current_theme.text_secondary,
                              0);
  lv_obj_set_style_text_color(s_pmkid_badge,
                              (update & FLAG_PMKID) ? current_theme.border_accent
                                                    : current_theme.text_secondary,
                              0);
  lv_label_set_text_fmt(
      s_queue_status,
      "Tries: %u | Failed: %u\nQueue: %lu/64 | Peak: %lu\nLost: %lu | SD failed: %lu",
      (unsigned)attempts,
      (unsigned)failures,
      (unsigned long)saved.pending,
      (unsigned long)saved.queue_peak,
      (unsigned long)saved.dropped,
      (unsigned long)saved.discarded);
  if (update & FLAG_FAILED)
    lv_label_set_text(s_status, "Capture stopped: SD/bridge error\nBACK to exit and retry");
  char text[LOG_BUFFER_SIZE];
  size_t used = 0;
  if (update & FLAG_HANDSHAKE)
    used += snprintf(text + used,
                     sizeof(text) - used,
                     "EAPOL pair observed: %02X:%02X:%02X:%02X:%02X:%02X\n",
                     hs[0],
                     hs[1],
                     hs[2],
                     hs[3],
                     hs[4],
                     hs[5]);
  if (update & FLAG_PMKID)
    used += snprintf(text + used,
                     sizeof(text) - used,
                     "PMKID observed: %02X:%02X:%02X:%02X:%02X:%02X\n",
                     pm[0],
                     pm[1],
                     pm[2],
                     pm[3],
                     pm[4],
                     pm[5]);
  if (used == 0)
    snprintf(text, sizeof(text), "Waiting for EAPOL pair or PMKID");
  lv_label_set_text(s_observed, text);
  used = 0;
  for (size_t i = 0; i < saved.count; i++) {
    const wifi_ap_capture_saved_t *ap = &saved.recent[i];
    int count = snprintf(text + used,
                         sizeof(text) - used,
                         "%s\n%lu EAPOL saved%s\n\n",
                         ap->filename,
                         (unsigned long)ap->frames,
                         ap->has_ssid ? "" : " - name pending");
    if (count < 0 || (size_t)count >= sizeof(text) - used)
      break;
    used += (size_t)count;
  }
  if (saved.count == 0)
    snprintf(text, sizeof(text), "No files saved yet.\nReconnect a client to your network.");
  lv_label_set_text(s_log, text);
  menu_component_scroll_content(&s_scroll, 0);
}

static void poll_worker(void *arg) {
  uint32_t generation = (uint32_t)(uintptr_t)arg;
  wifi_service_start();
  if (!s_is_poll_running || generation != atomic_load(&s_generation))
    goto cleanup;
  if (!wifi_sniffer_start(WIFI_SNIFFER_TYPE_EAPOL, MONITOR_CHANNEL)) {
    ESP_LOGE(TAG, "Capture start failed; check SD/session/bridge errors");
    led_signal_error();
    ui_async_call(state_ready_cb, (void *)(uintptr_t)(generation | FLAG_FAILED));
    goto cleanup;
  }

  while (s_is_poll_running && generation == atomic_load(&s_generation)) {
    uint32_t flags = 0;
    wifi_sniffer_stats_t stats = {0};
    if (!wifi_sniffer_get_stats(&stats) || wifi_sniffer_flush_capture() != ESP_OK) {
      publish_snapshot(&stats);
      ESP_LOGE(TAG, "Capture stopped: bridge or SD write failure");
      led_signal_error();
      ui_async_call(state_ready_cb, (void *)(uintptr_t)(generation | FLAG_FAILED));
      break;
    }
    if (stats.handshake_captured) {
      flags |= FLAG_HANDSHAKE;
      uint8_t bssid[6];
      wifi_sniffer_get_handshake_bssid(bssid);
      portENTER_CRITICAL(&s_snapshot_lock);
      memcpy(s_hs_bssid, bssid, sizeof(bssid));
      portEXIT_CRITICAL(&s_snapshot_lock);
    }
    if (stats.pmkid_captured) {
      flags |= FLAG_PMKID;
      uint8_t bssid[6];
      wifi_sniffer_get_pmkid_bssid(bssid);
      portENTER_CRITICAL(&s_snapshot_lock);
      memcpy(s_pm_bssid, bssid, sizeof(bssid));
      portEXIT_CRITICAL(&s_snapshot_lock);
    }
    publish_snapshot(&stats);
    ui_async_call(state_ready_cb, (void *)(uintptr_t)(generation | flags));
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }

  wifi_sniffer_stop();
cleanup:
  s_is_worker_busy = false;
  ui_async_call(capture_stopped_cb, (void *)(uintptr_t)generation);
  vTaskDeleteWithCaps(NULL);
}

void ui_wifi_handshake_stop(void) {
  s_is_poll_running = false;
  atomic_fetch_add(&s_generation, GENERATION_STEP);
}

static void screen_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen)
    ui_wifi_handshake_stop();
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_text(label, text);
  return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, bool is_accent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(
      card, is_accent ? current_theme.border_accent : current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(card, CARD_PADDING, 0);
  lv_obj_set_style_pad_row(card, CARD_GAP, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  return card;
}

static lv_obj_t *make_pair_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return row;
}

static void wifi_handshake_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS)
    return;
  if (ev->button == INPUT_BTN_BACK) {
    if (s_is_worker_busy && s_worker_generation == s_generation) {
      s_is_exit_requested = true;
      s_is_poll_running = false;
      lv_label_set_text(s_status, "Finishing SD writes...");
    } else {
      ui_wifi_handshake_stop();
      ui_switch_screen(SCREEN_WIFI_MENU);
    }
  } else if (ev->button == INPUT_BTN_UP) {
    menu_component_scroll_content(&s_scroll, -SCROLL_STEP);
  } else if (ev->button == INPUT_BTN_DOWN) {
    menu_component_scroll_content(&s_scroll, SCROLL_STEP);
  }
}

void ui_wifi_handshake_open(void) {
  ui_wifi_handshake_stop();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_is_exit_requested = false;
  s_screen = lv_obj_create(NULL);
  lv_obj_add_event_cb(s_screen, screen_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  ui_chrome_header(s_screen, "HANDSHAKE CAPTURE", NULL);
  ui_chrome_footer(s_screen, "UP/DOWN: Scroll | BACK: Exit");
  s_body = lv_obj_create(s_screen);
  lv_obj_set_size(s_body,
                  ui_screen_w() - CONTENT_MARGIN - SCROLL_GUTTER,
                  ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_body, LV_ALIGN_TOP_LEFT, CONTENT_MARGIN, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_body, 0, 0);
  lv_obj_set_style_pad_all(s_body, CONTENT_MARGIN, 0);
  lv_obj_set_style_pad_row(s_body, CONTENT_GAP, 0);
  lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
  lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
  lv_obj_t *live = make_card(s_body, true);
  s_channel_label = make_label(live, LV_SYMBOL_WIFI "  AUTO CAPTURE", current_theme.border_accent);
  s_status = make_label(live, "Starting capture...", current_theme.text_main);
  lv_obj_t *counters = make_pair_row(s_body);
  lv_obj_t *received = make_card(counters, false);
  lv_obj_set_width(received, LV_PCT(COUNTER_WIDTH_PERCENT));
  make_label(received, LV_SYMBOL_DOWNLOAD "  RECEIVED", current_theme.text_secondary);
  s_summary = make_label(received, "0", current_theme.text_main);
  lv_obj_set_style_text_font(s_summary, &lv_font_montserrat_16, 0);
  make_label(received, "packets", current_theme.text_secondary);
  lv_obj_t *saved = make_card(counters, false);
  lv_obj_set_width(saved, LV_PCT(COUNTER_WIDTH_PERCENT));
  make_label(saved, LV_SYMBOL_SAVE "  SAVED", current_theme.text_secondary);
  s_saved_count = make_label(saved, "0", current_theme.border_accent);
  lv_obj_set_style_text_font(s_saved_count, &lv_font_montserrat_16, 0);
  s_saved_size = make_label(saved, "0 bytes on SD", current_theme.text_secondary);
  lv_obj_t *observed = make_card(s_body, false);
  lv_obj_t *badges = make_pair_row(observed);
  s_pair_badge = make_label(badges, "-- PAIR", current_theme.text_secondary);
  lv_obj_set_width(s_pair_badge, LV_PCT(COUNTER_WIDTH_PERCENT));
  s_pmkid_badge = make_label(badges, "-- PMKID", current_theme.text_secondary);
  lv_obj_set_width(s_pmkid_badge, LV_PCT(COUNTER_WIDTH_PERCENT));
  s_observed = make_label(observed, "Waiting for authentication", current_theme.text_secondary);
  make_label(s_body, "RECENT CAPTURES", current_theme.border_accent);
  s_log = make_label(s_body, "No files saved yet.", current_theme.text_main);
  lv_obj_t *storage = make_card(s_body, false);
  make_label(storage, LV_SYMBOL_DIRECTORY "  PCAP STORAGE", current_theme.border_accent);
  make_label(storage, "/sdcard/wifi/loot/handshakes/", current_theme.text_secondary);
  s_queue_status = make_label(storage, "Queue: 0/64 | Lost: 0", current_theme.text_secondary);
  make_label(storage,
             "Saved EAPOL may be partial. Badges show C5 observations.",
             current_theme.text_secondary);
  memset(&s_scroll, 0, sizeof(s_scroll));
  menu_component_attach_scroll(&s_scroll, s_screen, s_body);
  menu_component_scroll_content(&s_scroll, 0);
  ui_input_set_screen_handler(wifi_handshake_input, NULL);
  if (!s_is_worker_busy) {
    portENTER_CRITICAL(&s_snapshot_lock);
    memset(&s_saved, 0, sizeof(s_saved));
    s_capture_attempts = 0;
    s_capture_failures = 0;
    s_capture_hold_seconds = 0;
    s_capture_ap_attempt = 0;
    s_packets = 0;
    s_channel = 0;
    portEXIT_CRITICAL(&s_snapshot_lock);
    s_is_poll_running = true;
    s_worker_generation = atomic_load(&s_generation);
    s_is_worker_busy = true;
    if (xTaskCreatePinnedToCoreWithCaps(poll_worker,
                                        "hs_poll",
                                        TASK_STACK_SIZE,
                                        (void *)(uintptr_t)atomic_load(&s_generation),
                                        TASK_PRIORITY,
                                        NULL,
                                        SYS_CORE_RADIO,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
      s_is_worker_busy = false;
      s_is_poll_running = false;
      ESP_LOGE(TAG, "Handshake worker allocation failed");
      led_signal_error();
      lv_label_set_text(s_status, "No memory - BACK to exit");
    }
  } else {
    lv_label_set_text(s_status, "Previous capture is stopping.\nBACK to exit and retry.");
    ESP_LOGW(TAG, "Previous capture is still stopping");
    led_signal_warning();
  }
  ui_screen_load_owned(&s_screen, s_screen);
}

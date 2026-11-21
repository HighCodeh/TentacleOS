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

#include "pc_monitor_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include "host_link.h"
#include "host_link_ble.h"
#include "host_link_sec.h"
#include "host_pc_monitor.h"
#include "led_control.h"
#include "psk_qr_ui.h"
#include "st7789.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "ui_metrics.h"

#define REFRESH_MS    500
#define CONTENT_PAD   8
#define ROW_GAP       4
#define BAR_HEIGHT    5
#define SCROLL_STEP   40
#define PERCENT_SCALE 100
#define MIB_PER_GIB   1024
#define METRIC_COUNT  3
#define CPU_INDEX     0
#define RAM_INDEX     1
#define DISK_INDEX    2

static const char *TAG = "PC_MONITOR_UI";
static lv_obj_t *s_screen;
static lv_obj_t *s_content;
static lv_obj_t *s_metrics;
static lv_obj_t *s_footer;
static lv_obj_t *s_status;
static lv_obj_t *s_values[METRIC_COUNT];
static lv_obj_t *s_bars[METRIC_COUNT];
static lv_obj_t *s_addresses;
static lv_timer_t *s_timer;
static bool s_started_ble;
static bool s_ble_failed;

static lv_obj_t *make_label(const char *text);
static void refresh(lv_timer_t *timer);
static void handle_input(const input_event_t *event, void *ctx);

void pc_monitor_ui_open(void) {
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_screen, "PC MONITOR", "/assets/icons/monitoring.bin");
  s_footer = ui_chrome_footer(s_screen, "OK QR / key    BACK Exit");
  s_content = lv_obj_create(s_screen);
  lv_obj_set_size(s_content, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_set_pos(s_content, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_content, 0, 0);
  lv_obj_set_style_pad_all(s_content, 0, 0);
  lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
  char psk[HOST_LINK_PSK_HEX_SIZE];
  bool has_psk = host_link_sec_get_psk_hex(psk, sizeof(psk)) == ESP_OK;
  psk_qr_ui_create(s_content, has_psk ? psk : NULL, "Waiting for PC...");
  s_metrics = lv_obj_create(s_content);
  lv_obj_set_size(s_metrics, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(s_metrics, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_metrics, 0, 0);
  lv_obj_set_style_pad_all(s_metrics, CONTENT_PAD, 0);
  lv_obj_set_style_pad_row(s_metrics, ROW_GAP, 0);
  lv_obj_set_flex_flow(s_metrics, LV_FLEX_FLOW_COLUMN);
  s_status = make_label("Connect USB / BLE and run CLI");
  static const char *const TITLES[METRIC_COUNT] = {"CPU --", "RAM --", "DISK --"};
  for (size_t i = 0; i < METRIC_COUNT; i++) {
    s_values[i] = make_label(TITLES[i]);
    s_bars[i] = lv_bar_create(s_metrics);
    lv_obj_set_size(s_bars[i], lv_pct(100), BAR_HEIGHT);
    lv_bar_set_range(s_bars[i], 0, HOST_PC_MONITOR_PERCENT_MAX);
    lv_obj_set_style_bg_color(s_bars[i], current_theme.border_accent, LV_PART_INDICATOR);
  }
  s_addresses = make_label("Local IP: --\nPublic IP: --");
  ui_input_set_screen_handler(handle_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
  s_started_ble = false;
  s_ble_failed = false;
  if (s_timer == NULL) {
    ESP_LOGE(TAG, "Cannot create refresh timer");
    led_signal_error();
    lv_obj_add_flag(s_metrics, LV_OBJ_FLAG_HIDDEN);
    psk_qr_ui_set_status(lv_obj_get_child(s_content, 0), "Status unavailable - reopen");
    return;
  }
  s_started_ble = !host_link_ble_is_active();
  if (s_started_ble) {
    s_ble_failed = host_link_ble_init() != ESP_OK || host_link_ble_start() != ESP_OK;
    if (s_ble_failed) {
      ESP_LOGW(TAG, "BLE unavailable; retry with OK or use USB");
      led_signal_warning();
    }
  }
  refresh(NULL);
  ESP_LOGI(TAG, "PC monitor opened");
}

void pc_monitor_ui_stop(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (!host_link_sec_is_authenticated()) {
    if (s_started_ble)
      host_link_ble_stop();
  }
  s_started_ble = false;
  if (s_screen != NULL)
    lv_obj_clean(s_content);
}

static lv_obj_t *make_label(const char *text) {
  lv_obj_t *label = lv_label_create(s_metrics);
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  lv_label_set_text(label, text);
  return label;
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  bool is_authenticated = host_link_sec_is_authenticated();
  lv_obj_t *pairing = lv_obj_get_child(s_content, 0);
  lv_obj_set_flag(pairing, LV_OBJ_FLAG_HIDDEN, is_authenticated);
  lv_obj_set_flag(s_metrics, LV_OBJ_FLAG_HIDDEN, !is_authenticated);
  if (!is_authenticated) {
    psk_qr_ui_set_status(pairing,
                         s_ble_failed ? "BLE unavailable / USB ready" : "Waiting for PC...");
    ui_chrome_footer_set_text(
        s_footer, s_ble_failed ? "OK retry BLE    BACK Exit" : "OK QR / key    BACK Exit");
    return;
  }
  ui_chrome_footer_set_text(s_footer, "UP/DOWN scroll    BACK Exit");
  host_pc_monitor_snapshot_t sample;
  if (!host_pc_monitor_get(&sample)) {
    lv_label_set_text(s_status, "Waiting for PC data / stale");
    lv_label_set_text(s_values[CPU_INDEX], "CPU --");
    lv_label_set_text(s_values[RAM_INDEX], "RAM --");
    lv_label_set_text(s_values[DISK_INDEX], "DISK --");
    lv_label_set_text(s_addresses, "Local IP: --\nPublic IP: --");
    for (size_t i = 0; i < METRIC_COUNT; i++)
      lv_bar_set_value(s_bars[i], 0, LV_ANIM_OFF);
    return;
  }
  lv_label_set_text(s_status, sample.hostname);
  lv_label_set_text_fmt(
      s_values[CPU_INDEX], "CPU %u.%02u%%", sample.cpu / PERCENT_SCALE, sample.cpu % PERCENT_SCALE);
  lv_label_set_text_fmt(s_values[RAM_INDEX],
                        "RAM %u%%  %lu / %lu MiB",
                        sample.ram / PERCENT_SCALE,
                        (unsigned long)sample.ram_used_mib,
                        (unsigned long)sample.ram_total_mib);
  lv_label_set_text_fmt(s_values[DISK_INDEX],
                        "DISK %u%%  %lu / %lu GiB\n%s",
                        sample.disk / PERCENT_SCALE,
                        (unsigned long)(sample.disk_used_mib / MIB_PER_GIB),
                        (unsigned long)(sample.disk_total_mib / MIB_PER_GIB),
                        sample.volume);
  lv_bar_set_value(s_bars[CPU_INDEX], sample.cpu, LV_ANIM_OFF);
  lv_bar_set_value(s_bars[RAM_INDEX], sample.ram, LV_ANIM_OFF);
  lv_bar_set_value(s_bars[DISK_INDEX], sample.disk, LV_ANIM_OFF);
  lv_label_set_text_fmt(s_addresses,
                        "Local IP: %s\nPublic IP: %s",
                        sample.local_ip[0] ? sample.local_ip : "unavailable",
                        sample.public_ip[0] ? sample.public_ip : "unavailable");
}

static void handle_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (event->action != INPUT_ACTION_PRESS && event->action != INPUT_ACTION_REPEAT)
    return;
  if ((event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN) &&
      host_link_sec_is_authenticated()) {
    lv_obj_scroll_by(
        s_metrics, 0, event->button == INPUT_BTN_UP ? SCROLL_STEP : -SCROLL_STEP, LV_ANIM_OFF);
  } else if (event->action == INPUT_ACTION_PRESS && event->button == INPUT_BTN_BACK) {
    ui_switch_screen(SCREEN_DEV_MENU);
  } else if (event->action == INPUT_ACTION_PRESS && event->button == INPUT_BTN_OK) {
    if (s_timer == NULL)
      return;
    if (!host_link_sec_is_authenticated()) {
      if (s_ble_failed)
        s_ble_failed = host_link_ble_init() != ESP_OK || host_link_ble_start() != ESP_OK;
      else
        psk_qr_ui_toggle_key(lv_obj_get_child(s_content, 0));
    }
    refresh(NULL);
  }
}

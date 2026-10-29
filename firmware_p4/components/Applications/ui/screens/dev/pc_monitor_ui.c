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
#include "st7789.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

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
static lv_obj_t *s_status;
static lv_obj_t *s_values[METRIC_COUNT];
static lv_obj_t *s_bars[METRIC_COUNT];
static lv_obj_t *s_addresses;
static lv_timer_t *s_timer;
static uint32_t s_pairing_token;
static bool s_started_ble;
static bool s_ble_failed;

static lv_obj_t *make_label(const char *text);
static void refresh(lv_timer_t *timer);
static void handle_input(const input_event_t *event, void *ctx);

void pc_monitor_ui_open(void) {
  host_link_sec_pairing_window(true);
  s_started_ble = !host_link_ble_is_active();
  s_ble_failed = false;
  if (s_started_ble) {
    s_ble_failed = host_link_ble_init() != ESP_OK || host_link_ble_start() != ESP_OK;
    if (s_ble_failed)
      ESP_LOGW(TAG, "BLE unavailable; retry with OK or use USB");
  }
  s_pairing_token = 0;
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_screen, "PC MONITOR", "/assets/icons/monitoring.bin");
  ui_chrome_footer(s_screen, "OK pair  UP/DOWN scroll  BACK");
  s_content = lv_obj_create(s_screen);
  lv_obj_set_size(s_content, lv_pct(100), LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_set_pos(s_content, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_content, 0, 0);
  lv_obj_set_style_pad_all(s_content, CONTENT_PAD, 0);
  lv_obj_set_style_pad_row(s_content, ROW_GAP, 0);
  lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
  s_status = make_label("Connect USB / BLE and run CLI");
  static const char *const TITLES[METRIC_COUNT] = {"CPU --", "RAM --", "DISK --"};
  for (size_t i = 0; i < METRIC_COUNT; i++) {
    s_values[i] = make_label(TITLES[i]);
    s_bars[i] = lv_bar_create(s_content);
    lv_obj_set_size(s_bars[i], lv_pct(100), BAR_HEIGHT);
    lv_bar_set_range(s_bars[i], 0, HOST_PC_MONITOR_PERCENT_MAX);
    lv_obj_set_style_bg_color(s_bars[i], current_theme.border_accent, LV_PART_INDICATOR);
  }
  s_addresses = make_label("Local IP: --\nPublic IP: --");
  ui_input_set_screen_handler(handle_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
  if (s_timer == NULL)
    ESP_LOGE(TAG, "Cannot create refresh timer");
  refresh(NULL);
  ESP_LOGI(TAG, "PC monitor opened");
}

void pc_monitor_ui_stop(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  host_link_sec_pairing_window(false);
  s_pairing_token = 0;
  if (!host_link_sec_is_authenticated()) {
    host_link_reset_rx();
    if (s_started_ble)
      host_link_ble_stop();
  }
  s_started_ble = false;
}

static lv_obj_t *make_label(const char *text) {
  lv_obj_t *label = lv_label_create(s_content);
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  lv_label_set_text(label, text);
  return label;
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  host_link_sec_pairing_refresh();
  uint32_t previous_token = s_pairing_token;
  host_link_pairing_t pairing = {0};
  bool has_pairing = host_link_sec_pairing_get(&pairing);
  s_pairing_token = has_pairing ? pairing.token : 0;
  if (has_pairing && pairing.token != previous_token)
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
  host_pc_monitor_snapshot_t sample;
  if (!host_pc_monitor_get(&sample)) {
    if (has_pairing)
      lv_label_set_text_fmt(s_status,
                            "Compare code: %06lu\n%s",
                            (unsigned long)pairing.code,
                            pairing.is_approved ? "Confirming..." : "OK approve / BACK cancel");
    else
      lv_label_set_text(s_status,
                        host_link_sec_is_authenticated()
                            ? "Waiting for PC data / stale"
                            : (s_ble_failed
                                   ? "BLE unavailable / OK retry\nUSB remains available"
                                   : "USB / BLE: ready to pair\nSelect this Highboy in the CLI"));
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
  if (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN) {
    lv_obj_scroll_by(
        s_content, 0, event->button == INPUT_BTN_UP ? SCROLL_STEP : -SCROLL_STEP, LV_ANIM_OFF);
  } else if (event->action == INPUT_ACTION_PRESS && event->button == INPUT_BTN_BACK) {
    ui_switch_screen(SCREEN_DEV_MENU);
  } else if (event->action == INPUT_ACTION_PRESS && event->button == INPUT_BTN_OK) {
    if (s_pairing_token != 0) {
      if (host_link_sec_pairing_confirm(s_pairing_token, true) == ESP_OK)
        ui_feedback(UI_FB_SELECT);
    } else if (!host_link_sec_is_authenticated()) {
      host_link_sec_pairing_window(true);
      if (s_ble_failed) {
        s_ble_failed = host_link_ble_init() != ESP_OK || host_link_ble_start() != ESP_OK;
      }
    }
    refresh(NULL);
  }
}

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

#include "wifi_evil_twin_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "WIFI_EVIL_TWIN_UI";

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2
#define NAV_TIMER_INTERVAL_MS  50
#define VICTIM_TICK_MS         700
#define VICTIM_TOTAL           6
#define LOG_ROWS               3
#define ROGUE_CHANNEL          6

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_victim_timer = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_waves = NULL;
static lv_obj_t *s_log_rows[LOG_ROWS] = {NULL};
static int s_clients = 0;
static uint32_t s_mac_seed = 0x5A17C0DE;

static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *timer);
static void victim_tick_cb(lv_timer_t *timer);
static void stop_victim_timer(void);

static uint32_t next_rand(void) {
  s_mac_seed = s_mac_seed * 1664525u + 1013904223u;
  return s_mac_seed;
}

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void blink_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void attach_blink(lv_obj_t *obj, uint32_t period_ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, blink_opa_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40);
  lv_anim_set_duration(&a, period_ms);
  lv_anim_set_playback_duration(&a, period_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

void ui_wifi_evil_twin_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_clients = 0;
  s_victim_timer = NULL;
  for (int i = 0; i < LOG_ROWS; i++)
    s_log_rows[i] = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "EVIL TWIN", "/assets/icons/wifi_tethering.bin");

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, "Broadcasting...");
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 12);

  s_ssid_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_ssid_label, "SSID: FreeWiFi_5G  CH %d", ROGUE_CHANNEL);
  lv_obj_set_style_text_color(s_ssid_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_ssid_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_ssid_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_ssid_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 34);

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, 8, NULL, NULL);

  s_count_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_count_label, "Clients: %d", s_clients);
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_count_label, LV_ALIGN_BOTTOM_MID, 0, -LOG_ROWS * 16 - 10 - UI_CHROME_FOOTER_H);

  for (int i = 0; i < LOG_ROWS; i++) {
    s_log_rows[i] = lv_label_create(s_screen);
    lv_label_set_text(s_log_rows[i], "");
    lv_obj_set_style_text_color(s_log_rows[i], current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(s_log_rows[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_log_rows[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(
        s_log_rows[i], LV_ALIGN_BOTTOM_MID, 0, -((LOG_ROWS - 1 - i) * 16) - 6 - UI_CHROME_FOOTER_H);
  }

  ui_chrome_footer(s_screen, "BACK: Cancel");

  fade_in(s_status_label, 200);
  fade_in(s_ssid_label, 200);
  fade_in(s_count_label, 200);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);
  s_victim_timer = lv_timer_create(victim_tick_cb, VICTIM_TICK_MS, NULL);

  ui_screen_load(s_screen);
}

static void log_push(const char *line) {
  lv_color_t joined = lv_palette_main(LV_PALETTE_GREEN);
  for (int i = 0; i < LOG_ROWS - 1; i++) {
    if (s_log_rows[i] != NULL && s_log_rows[i + 1] != NULL)
      lv_label_set_text(s_log_rows[i], lv_label_get_text(s_log_rows[i + 1]));
  }
  if (s_log_rows[LOG_ROWS - 1] != NULL)
    lv_label_set_text(s_log_rows[LOG_ROWS - 1], line);

  for (int i = 0; i < LOG_ROWS; i++) {
    if (s_log_rows[i] == NULL)
      continue;
    const char *txt = lv_label_get_text(s_log_rows[i]);
    bool populated = (txt != NULL && txt[0] != '\0');
    lv_obj_set_style_text_color(
        s_log_rows[i], populated ? joined : current_theme.border_inactive, 0);
  }
}

static void stop_victim_timer(void) {
  if (s_victim_timer != NULL) {
    lv_timer_delete(s_victim_timer);
    s_victim_timer = NULL;
  }
}

static void victim_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_victim_timer == timer)
      s_victim_timer = NULL;
    return;
  }

  if (s_clients < VICTIM_TOTAL) {
    s_clients++;
    lv_label_set_text_fmt(s_count_label, "Clients: %d", s_clients);

    uint32_t a = next_rand();
    uint32_t b = next_rand();
    char line[40];
    snprintf(line,
             sizeof(line),
             "%02X:%02X:%02X:%02X:%02X:%02X joined",
             (unsigned)(a & 0xFF),
             (unsigned)((a >> 8) & 0xFF),
             (unsigned)((a >> 16) & 0xFF),
             (unsigned)(b & 0xFF),
             (unsigned)((b >> 8) & 0xFF),
             (unsigned)((b >> 16) & 0xFF));
    log_push(line);
  }

  if (s_clients >= VICTIM_TOTAL) {
    lv_label_set_text(s_status_label, "Captured!");
    lv_timer_delete(timer);
    s_victim_timer = NULL;
  }
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    stop_victim_timer();
    return;
  }
  if (ui_input_is_locked())
    return;

  bool is_back = back_button_is_down();
  if (is_back && !s_btn_back_last) {
    stop_victim_timer();
    ESP_LOGI(TAG, "mock evil-twin cancelled");
    ui_switch_screen(SCREEN_WIFI_MENU);
  }
  s_btn_back_last = is_back;
}

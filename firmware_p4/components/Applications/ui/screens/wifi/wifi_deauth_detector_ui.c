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

#include "wifi_deauth_detector_ui.h"

#include <stdint.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sys_prio.h"

#include "deauther_detector.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"

static const char *TAG = "WIFI_DEAUTH_DET_UI";

#define ENTRY_FADE_MS   240
#define BANNER_BLINK_MS 450

#define HEADER_ICON "/assets/icons/monitoring.bin"
#define COUNT_FONT  "A:assets/fonts/Inter.bin"

#define STATUS_TOP_Y (UI_CHROME_HEADER_H + 10)

#define CARD_W           190
#define CARD_H           112
#define CARD_RADIUS      16
#define CARD_TOP_Y       (UI_CHROME_HEADER_H + 44)
#define CARD_BORDER_W    1
#define CARD_GLOW_W      16
#define CARD_GLOW_SPREAD (-4)

#define BANNER_W        200
#define BANNER_H        30
#define BANNER_RADIUS   15
#define BANNER_BOTTOM_Y (-40)

#define ALERT_COLOR 0xE53935
#define CALM_COLOR  0x00E676

#define POLL_INTERVAL_MS 1000
#define TASK_STACK_SIZE  8192
#define TASK_PRIORITY    SYS_PRIO_SERVICE_LO

static lv_obj_t *s_screen = NULL;
static lv_font_t *s_big_font = NULL;

static lv_obj_t *s_card = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_banner = NULL;

static bool s_is_baseline_set = false;
static uint32_t s_last_count = 0;
static volatile bool s_is_poll_running = false;
static volatile bool s_is_worker_busy = false;

static void wifi_deauth_detector_input(const input_event_t *ev, void *ctx);
static void stop_detection(void);

static void banner_blink_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void attach_blink(lv_obj_t *obj, uint32_t period_ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, banner_blink_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40);
  lv_anim_set_duration(&a, period_ms);
  lv_anim_set_playback_duration(&a, period_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static void apply_state(bool alert) {
  lv_color_t accent = alert ? lv_color_hex(ALERT_COLOR) : lv_color_hex(CALM_COLOR);

  if (s_count_label != NULL)
    lv_obj_set_style_text_color(s_count_label, accent, 0);

  if (s_card != NULL) {
    lv_obj_set_style_border_color(s_card, accent, 0);
    lv_obj_set_style_shadow_color(s_card, accent, 0);
    lv_obj_set_style_shadow_opa(s_card, alert ? LV_OPA_60 : LV_OPA_30, 0);
  }

  if (s_status_label != NULL) {
    lv_label_set_text(s_status_label, alert ? "Deauth flood detected" : "Monitoring...");
    lv_obj_set_style_text_color(
        s_status_label, alert ? lv_color_hex(ALERT_COLOR) : current_theme.text_secondary, 0);
  }

  if (s_banner != NULL) {
    if (alert)
      lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  }
}

static void count_ready_cb(void *param) {
  if (ui_current_screen() != SCREEN_WIFI_DEAUTH_DETECTOR) {
    s_is_poll_running = false;
    return;
  }

  uint32_t count = (uint32_t)(uintptr_t)param;

  if (s_count_label != NULL)
    lv_label_set_text_fmt(s_count_label, "%lu", (unsigned long)count);

  bool alert;
  if (!s_is_baseline_set) {
    s_is_baseline_set = true;
    alert = false;
  } else {
    alert = (count > s_last_count);
  }
  s_last_count = count;

  apply_state(alert);
}

static void detector_worker(void *arg) {
  (void)arg;
  wifi_service_start();
  deauther_detector_start();

  while (s_is_poll_running) {
    uint32_t count = deauther_detector_get_count();
    ui_async_call(count_ready_cb, (void *)(uintptr_t)count);
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }

  deauther_detector_stop();
  s_is_worker_busy = false;
  vTaskDelete(NULL);
}

static void start_detection(void) {
  s_is_poll_running = true;
  if (!s_is_worker_busy) {
    s_is_worker_busy = true;
    if (xTaskCreatePinnedToCore(detector_worker,
                                "deauth_worker",
                                TASK_STACK_SIZE,
                                NULL,
                                TASK_PRIORITY,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_is_worker_busy = false;
      ESP_LOGW(TAG, "Failed to spawn deauth worker");
    }
  }
}

static void stop_detection(void) {
  s_is_poll_running = false;
}

void ui_wifi_deauth_detector_open(void) {
  stop_detection();

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
    s_card = NULL;
    s_count_label = NULL;
    s_status_label = NULL;
    s_banner = NULL;
  }

  s_is_baseline_set = false;
  s_last_count = 0;

  if (s_big_font == NULL)
    s_big_font = lv_binfont_create(COUNT_FONT);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "DEAUTH GUARD", HEADER_ICON);
  ui_chrome_footer(s_screen, "BACK: Exit");

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, "Monitoring...");
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_TOP_Y);

  s_card = lv_obj_create(s_screen);
  lv_obj_set_size(s_card, CARD_W, CARD_H);
  lv_obj_align(s_card, LV_ALIGN_TOP_MID, 0, CARD_TOP_Y);
  lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(s_card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(s_card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_card, CARD_BORDER_W, 0);
  lv_obj_set_style_shadow_width(s_card, CARD_GLOW_W, 0);
  lv_obj_set_style_shadow_spread(s_card, CARD_GLOW_SPREAD, 0);
  lv_obj_set_style_pad_all(s_card, 6, 0);
  lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_card, 4, 0);

  s_count_label = lv_label_create(s_card);
  lv_label_set_text(s_count_label, "0");
  lv_obj_set_style_text_font(s_count_label, s_big_font ? s_big_font : &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *caption = lv_label_create(s_card);
  lv_label_set_text(caption, "DEAUTH FRAMES");
  lv_obj_set_style_text_font(caption, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(caption, current_theme.text_secondary, 0);

  lv_obj_t *info = lv_label_create(s_screen);
  lv_label_set_text(info, "Watching channels 1-13");
  lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(info, current_theme.text_secondary, 0);
  lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(info, s_card, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);

  s_banner = lv_obj_create(s_screen);
  lv_obj_set_size(s_banner, BANNER_W, BANNER_H);
  lv_obj_align(s_banner, LV_ALIGN_BOTTOM_MID, 0, BANNER_BOTTOM_Y);
  lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(s_banner, BANNER_RADIUS, 0);
  lv_obj_set_style_bg_color(s_banner, lv_color_hex(ALERT_COLOR), 0);
  lv_obj_set_style_bg_opa(s_banner, LV_OPA_20, 0);
  lv_obj_set_style_border_width(s_banner, CARD_BORDER_W, 0);
  lv_obj_set_style_border_color(s_banner, lv_color_hex(ALERT_COLOR), 0);
  lv_obj_set_style_pad_all(s_banner, 0, 0);
  lv_obj_set_flex_flow(s_banner, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_banner, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *banner_label = lv_label_create(s_banner);
  lv_label_set_text(banner_label, LV_SYMBOL_WARNING " ALERT: Deauth flood");
  lv_obj_set_style_text_font(banner_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(banner_label, lv_color_hex(ALERT_COLOR), 0);

  attach_blink(s_banner, BANNER_BLINK_MS);

  apply_state(false);

  lv_obj_fade_in(s_screen, ENTRY_FADE_MS, 0);

  ESP_LOGI(TAG, "deauth detector open");

  ui_input_set_screen_handler(wifi_deauth_detector_input, NULL);

  start_detection();

  ui_screen_load_owned(&s_screen, s_screen);
}

static void wifi_deauth_detector_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        stop_detection();
        ui_switch_screen(SCREEN_WIFI_MENU);
      }
      break;
    default:
      break;
  }
}

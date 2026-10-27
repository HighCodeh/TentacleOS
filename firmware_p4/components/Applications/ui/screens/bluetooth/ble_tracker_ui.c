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

#include "ble_tracker_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "tracker_detector.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "BLE_TRACKER_UI";

#define POLL_MS 800

#define MAC_LEN        18
#define DETAIL_LEN     48
#define TRACKERS_SHOWN 3

#define COL_ALERT 0xFF5252

#define TRACK_ICON "/assets/icons/troubleshoot.bin"

#define BODY_W       240
#define BODY_H       LV_MIN(256, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define BODY_PAD     10
#define BODY_GAP     8
#define CARD_W       220
#define CARD_H       52
#define BANNER_H     46
#define CARD_PAD     8
#define TYPE_Y       2
#define DETAIL_Y     22
#define STATUS_Y_OFS 64
#define SUB_Y_OFS    88

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_poll_timer = NULL;
static bool s_is_showing_results = false;

static lv_obj_t *s_banner = NULL;

static void ble_tracker_input(const input_event_t *ev, void *ctx);
static void poll_cb(lv_timer_t *timer);
static void build_scanning_view(void);
static void build_results_view(void);

static void stop_detector(void) {
  if (s_poll_timer != NULL) {
    lv_timer_delete(s_poll_timer);
    s_poll_timer = NULL;
  }
  tracker_detector_stop();
}

static void clear_screen_children(void) {
  lv_obj_clean(s_screen);
  s_banner = NULL;
}

static lv_obj_t *body_container(void) {
  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_set_size(body, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_radius(body, 0, 0);
  lv_obj_set_style_pad_all(body, BODY_PAD, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(body, BODY_GAP, 0);
  return body;
}

static lv_obj_t *lit_card(lv_obj_t *parent, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, CARD_PAD, 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);
  return card;
}

static void make_tracker_card(lv_obj_t *parent, const char *type, const char *mac, int8_t rssi) {
  lv_obj_t *card = lit_card(parent, CARD_W, CARD_H);

  lv_obj_t *name = lv_label_create(card);
  lv_label_set_text(name, type);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, TYPE_Y);

  lv_obj_t *detail = lv_label_create(card);
  char buf[DETAIL_LEN];
  snprintf(buf, sizeof(buf), "%s   %d dBm", mac, rssi);
  lv_label_set_text(detail, buf);
  lv_obj_set_style_text_color(detail, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(detail, &lv_font_montserrat_12, 0);
  lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 0, DETAIL_Y);
}

static void build_scanning_view(void) {
  clear_screen_children();

  ui_chrome_header(s_screen, "TRACKER SCAN", TRACK_ICON);
  ui_chrome_footer(s_screen, "BACK Cancel");

  waves_create(s_screen, LV_ALIGN_CENTER, 0, -20, NULL, TRACK_ICON);

  lv_obj_t *status = lv_label_create(s_screen);
  lv_label_set_text(status, "Scanning for trackers");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, STATUS_Y_OFS);

  lv_obj_t *sub = lv_label_create(s_screen);
  lv_label_set_text(sub, "Hold still while we listen");
  lv_obj_set_style_text_color(sub, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, SUB_Y_OFS);
}

static void build_results_view(void) {
  clear_screen_children();

  ui_chrome_header(s_screen, "TRACKERS", TRACK_ICON);
  ui_chrome_footer(s_screen, "BACK Back");

  lv_obj_t *body = body_container();

  s_banner = lit_card(body, CARD_W, BANNER_H);
  lv_obj_set_style_bg_color(s_banner, lv_color_hex(COL_ALERT), 0);
  lv_obj_set_style_bg_opa(s_banner, LV_OPA_20, 0);
  lv_obj_set_style_border_color(s_banner, lv_color_hex(COL_ALERT), 0);
  lv_obj_set_style_shadow_color(s_banner, lv_color_hex(COL_ALERT), 0);
  lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *alert = lv_label_create(s_banner);
  lv_label_set_text(alert, LV_SYMBOL_WARNING "  TRACKER NEARBY");
  lv_obj_set_style_text_color(alert, lv_color_hex(COL_ALERT), 0);
  lv_obj_set_style_text_font(alert, &lv_font_montserrat_14, 0);
  lv_obj_align(alert, LV_ALIGN_LEFT_MID, 0, -8);

  lv_obj_t *alert_sub = lv_label_create(s_banner);
  lv_obj_set_style_text_color(alert_sub, current_theme.text_main, 0);
  lv_obj_set_style_text_font(alert_sub, &lv_font_montserrat_12, 0);
  lv_obj_align(alert_sub, LV_ALIGN_LEFT_MID, 0, 12);

  uint16_t count = 0;
  tracker_detector_record_t *rec = tracker_detector_get_results(&count);
  int shown = 0;
  for (uint16_t i = 0; i < count && shown < TRACKERS_SHOWN && rec != NULL; i++) {
    char mac[MAC_LEN];
    const char *type = (rec[i].type_str[0] != '\0') ? rec[i].type_str : "Tracker";
    snprintf(mac,
             sizeof(mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             rec[i].addr[5],
             rec[i].addr[4],
             rec[i].addr[3],
             rec[i].addr[2],
             rec[i].addr[1],
             rec[i].addr[0]);
    make_tracker_card(body, type, mac, rec[i].rssi);
    if (shown == 0)
      lv_label_set_text_fmt(alert_sub, "%s in range", type);
    shown++;
  }

  if (shown > 0) {
    lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_in(s_banner, 200, 0);
  }

  lv_obj_fade_in(body, 220, 0);
}

void ui_ble_tracker_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_poll_timer = NULL;
  s_banner = NULL;
  s_is_showing_results = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  build_scanning_view();

  ui_input_set_screen_handler(ble_tracker_input, NULL);

  if (!tracker_detector_start()) {
    notify(NOTIFY_WARNING, "Radio unavailable");
    ESP_LOGE(TAG, "tracker_detector_start failed");
  } else {
    s_poll_timer = lv_timer_create(poll_cb, POLL_MS, NULL);
  }

  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "BLE tracker screen opened");
}

static void poll_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    stop_detector();
    return;
  }

  uint16_t count = 0;
  (void)tracker_detector_get_results(&count);
  if (count == 0)
    return;

  static uint16_t s_last_shown = 0;
  if (!s_is_showing_results || count != s_last_shown) {
    bool first = !s_is_showing_results;
    s_is_showing_results = true;
    s_last_shown = count;
    build_results_view();
    if (first)
      ui_feedback(UI_FB_READ);
  }
}

static void ble_tracker_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        stop_detector();
        ui_switch_screen(SCREEN_BLE_DETECT_MENU);
      }
      break;
    default:
      break;
  }
}

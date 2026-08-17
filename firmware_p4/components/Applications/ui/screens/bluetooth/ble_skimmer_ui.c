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

#include "ble_skimmer_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "skimmer_detector.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "BLE_SKIMMER_UI";

#define POLL_MS 800

#define MAC_LEN        18
#define NAME_LEN       24
#define DETAIL_LEN     48
#define SUSPECTS_SHOWN 3

#define COL_DIM    0x8A8594
#define COL_THREAT 0xFFB300

#define SKIM_ICON "/assets/icons/warning.bin"

#define BODY_W       240
#define BODY_H       256
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
static lv_timer_t *s_scan_timer = NULL;
static bool s_is_showing_results = false;

static void ble_skimmer_input(const input_event_t *ev, void *ctx);
static void poll_cb(lv_timer_t *timer);
static void build_scanning_view(void);
static void build_results_view(void);

static void stop_detector(void) {
  if (s_scan_timer != NULL) {
    lv_timer_delete(s_scan_timer);
    s_scan_timer = NULL;
  }
  skimmer_detector_stop();
}

static lv_obj_t *body_container(void) {
  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_set_size(body, BODY_W, BODY_H);
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

static void make_suspect_card(lv_obj_t *parent, const char *name, const char *mac, int8_t rssi) {
  lv_obj_t *card = lit_card(parent, CARD_W, CARD_H);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, name);
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, TYPE_Y);

  lv_obj_t *detail = lv_label_create(card);
  char buf[DETAIL_LEN];
  snprintf(buf, sizeof(buf), "%s   %d dBm", mac, rssi);
  lv_label_set_text(detail, buf);
  lv_obj_set_style_text_color(detail, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(detail, &lv_font_montserrat_12, 0);
  lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 0, DETAIL_Y);
}

static void build_scanning_view(void) {
  lv_obj_clean(s_screen);

  ui_chrome_header(s_screen, "SKIMMER SCAN", SKIM_ICON);
  ui_chrome_footer(s_screen, "BACK Cancel");

  waves_create(s_screen, LV_ALIGN_CENTER, 0, -20, NULL, SKIM_ICON);

  lv_obj_t *status = lv_label_create(s_screen);
  lv_label_set_text(status, "Probing serial modules");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, STATUS_Y_OFS);

  lv_obj_t *sub = lv_label_create(s_screen);
  lv_label_set_text(sub, "Looking for HC / RNBT chips");
  lv_obj_set_style_text_color(sub, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, SUB_Y_OFS);
}

static void build_results_view(void) {
  lv_obj_clean(s_screen);

  ui_chrome_header(s_screen, "SKIMMERS", SKIM_ICON);
  ui_chrome_footer(s_screen, "BACK Back");

  lv_obj_t *body = body_container();

  lv_obj_t *banner = lit_card(body, CARD_W, BANNER_H);
  lv_obj_set_style_bg_color(banner, lv_color_hex(COL_THREAT), 0);
  lv_obj_set_style_bg_opa(banner, LV_OPA_20, 0);
  lv_obj_set_style_border_color(banner, lv_color_hex(COL_THREAT), 0);
  lv_obj_set_style_shadow_color(banner, lv_color_hex(COL_THREAT), 0);

  lv_obj_t *threat = lv_label_create(banner);
  lv_label_set_text(threat, LV_SYMBOL_WARNING "  SKIMMER SIGNATURE");
  lv_obj_set_style_text_color(threat, lv_color_hex(COL_THREAT), 0);
  lv_obj_set_style_text_font(threat, &lv_font_montserrat_14, 0);
  lv_obj_align(threat, LV_ALIGN_LEFT_MID, 0, -8);

  lv_obj_t *threat_sub = lv_label_create(banner);
  lv_label_set_text(threat_sub, "Serial BT modules nearby");
  lv_obj_set_style_text_color(threat_sub, current_theme.text_main, 0);
  lv_obj_set_style_text_font(threat_sub, &lv_font_montserrat_12, 0);
  lv_obj_align(threat_sub, LV_ALIGN_LEFT_MID, 0, 12);

  uint16_t count = 0;
  skimmer_detector_record_t *rec = skimmer_detector_get_results(&count);
  int shown = 0;
  for (uint16_t i = 0; i < count && shown < SUSPECTS_SHOWN && rec != NULL; i++) {
    char name[NAME_LEN];
    char mac[MAC_LEN];
    snprintf(name, sizeof(name), "%.23s", (rec[i].name[0] != '\0') ? rec[i].name : "(unknown)");
    snprintf(mac,
             sizeof(mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             rec[i].addr[5],
             rec[i].addr[4],
             rec[i].addr[3],
             rec[i].addr[2],
             rec[i].addr[1],
             rec[i].addr[0]);
    make_suspect_card(body, name, mac, rec[i].rssi);
    shown++;
  }

  lv_obj_fade_in(body, 220, 0);
  ui_feedback(UI_FB_READ);
  notify(NOTIFY_WARNING, "Possible skimmer detected");
}

void ui_ble_skimmer_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_scan_timer = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  s_is_showing_results = false;
  build_scanning_view();

  ui_input_set_screen_handler(ble_skimmer_input, NULL);

  if (!skimmer_detector_start()) {
    notify(NOTIFY_WARNING, "Radio unavailable");
    ESP_LOGE(TAG, "skimmer_detector_start failed");
  } else {
    s_scan_timer = lv_timer_create(poll_cb, POLL_MS, NULL);
  }

  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "BLE skimmer screen opened");
}

static void poll_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    stop_detector();
    return;
  }

  uint16_t count = 0;
  (void)skimmer_detector_get_results(&count);
  if (count == 0)
    return;

  static uint16_t s_last_shown = 0;
  if (!s_is_showing_results || count != s_last_shown) {
    s_is_showing_results = true;
    s_last_shown = count;
    build_results_view();
  }
}

static void ble_skimmer_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        stop_detector();
        ui_switch_screen(SCREEN_BLE_DETECT_MENU);
      }
      break;
    default:
      break;
  }
}

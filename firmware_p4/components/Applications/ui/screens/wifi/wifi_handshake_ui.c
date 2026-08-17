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

#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "st7789.h"
#include "sys_prio.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "WIFI_HANDSHAKE_UI";

#define POLL_INTERVAL_MS 1000
#define TASK_STACK_SIZE  8192
#define TASK_PRIORITY    SYS_PRIO_SERVICE_LO
#define MONITOR_CHANNEL  0

#define FLAG_HANDSHAKE 0x1u
#define FLAG_PMKID     0x2u

#define BSSID_STR_LEN 18

#define HDR_TITLE   "HANDSHAKE"
#define HDR_ICON    NULL
#define FOOTER_HINT "BACK: Exit"

#define MX        8
#define CONTENT_W (LCD_H_RES - 2 * MX)
#define BODY_TOP  UI_CHROME_HEADER_H
#define BODY_H    (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define STACK_GAP 7

#define CARD1_H     76
#define CARD1_PAD_H 10
#define CARD1_PAD_V 8
#define CARD_RADIUS 9
#define ICON_X      0
#define NAME_X      24
#define ROW1_Y      1
#define BSSID_Y     26
#define STA_Y       44
#define CHIP_H      17
#define CHIP_PAD_H  6

#define CARD2_H      96
#define CARD2_PAD    8
#define EAPOL_LBL_Y  0
#define WAVE_W       190
#define WAVE_H       30
#define WAVE_X       9
#define WAVE_Y       22
#define WAVE_LINE_W  2
#define BADGE_ROW_Y  58
#define BADGE_ROW_H  20
#define BADGE_GAP    5
#define BADGE_RADIUS 7
#define BADGE_COUNT  4

#define PMKID_ROW_H 16
#define SAVED_ROW_H 20
#define SAVED_GAP   6
#define DOT_SIZE    9

#define COL_OK  0x00E676
#define COL_DIM 0x8A8594

#define AP_NAME       "Monitor"
#define AP_CHAN       "HOP"
#define LBL_BSSID     "BSSID"
#define LBL_STA       "STA"
#define LBL_EAPOL     "EAPOL 4-WAY"
#define TXT_CAPTURING "capturing"
#define TXT_COMPLETE  "complete"
#define TXT_CAPTURED  "captured"
#define LBL_PMKID     "PMKID"
#define VAL_NONE      "--"
#define BSSID_NONE    "--:--:--:--:--:--"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_badge[BADGE_COUNT];
static lv_obj_t *s_badge_box[BADGE_COUNT];
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_saved_dot = NULL;
static lv_obj_t *s_saved_txt = NULL;
static lv_obj_t *s_bssid_val = NULL;
static lv_obj_t *s_pmkid_val = NULL;

static volatile bool s_is_poll_running = false;
static volatile bool s_is_worker_busy = false;
static uint8_t s_hs_bssid[6];
static uint8_t s_pm_bssid[6];

static void wifi_handshake_input(const input_event_t *ev, void *ctx);
static void stop_detection(void);

static const lv_point_precise_t WAVE_PTS[] = {
    {0, 22},
    {18, 22},
    {18, 6},
    {30, 6},
    {30, 22},
    {60, 22},
    {60, 6},
    {72, 6},
    {72, 22},
    {102, 22},
    {102, 6},
    {114, 6},
    {114, 22},
    {144, 22},
    {144, 6},
    {156, 6},
    {156, 22},
    {190, 22},
};
#define WAVE_PT_COUNT ((int)(sizeof(WAVE_PTS) / sizeof(WAVE_PTS[0])))

static void style_badge(int i, bool lit) {
  lv_color_t c = lit ? lv_color_hex(COL_OK) : lv_color_hex(COL_DIM);
  lv_obj_set_style_text_color(s_badge[i], c, 0);
  lv_obj_set_style_border_color(s_badge_box[i], c, 0);
  lv_obj_set_style_border_opa(s_badge_box[i], lit ? LV_OPA_40 : LV_OPA_20, 0);
}

static void set_progress(int lit) {
  for (int i = 0; i < BADGE_COUNT; i++)
    style_badge(i, i < lit);

  bool done = (lit >= BADGE_COUNT);
  lv_label_set_text(s_status, done ? TXT_COMPLETE : TXT_CAPTURING);
  lv_obj_set_style_text_color(s_status, done ? lv_color_hex(COL_OK) : lv_color_hex(COL_DIM), 0);

  lv_obj_set_style_bg_color(s_saved_dot, done ? lv_color_hex(COL_OK) : lv_color_hex(COL_DIM), 0);
  lv_label_set_text(s_saved_txt, done ? TXT_CAPTURED : TXT_CAPTURING);
}

static void format_bssid(char *buf, size_t len, const uint8_t *b) {
  snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

static void state_ready_cb(void *param) {
  if (ui_current_screen() != SCREEN_WIFI_HANDSHAKE) {
    s_is_poll_running = false;
    return;
  }
  if (s_status == NULL)
    return;

  uint32_t flags = (uint32_t)(uintptr_t)param;
  bool hs = (flags & FLAG_HANDSHAKE) != 0;
  bool pm = (flags & FLAG_PMKID) != 0;

  set_progress(hs ? BADGE_COUNT : 0);

  if (s_bssid_val != NULL) {
    if (hs) {
      char buf[BSSID_STR_LEN];
      format_bssid(buf, sizeof(buf), s_hs_bssid);
      lv_label_set_text(s_bssid_val, buf);
      lv_obj_set_style_text_color(s_bssid_val, current_theme.text_main, 0);
    } else {
      lv_label_set_text(s_bssid_val, BSSID_NONE);
    }
  }

  if (s_pmkid_val != NULL) {
    if (pm) {
      char buf[BSSID_STR_LEN];
      format_bssid(buf, sizeof(buf), s_pm_bssid);
      lv_label_set_text(s_pmkid_val, buf);
      lv_obj_set_style_text_color(s_pmkid_val, current_theme.border_accent, 0);
    } else {
      lv_label_set_text(s_pmkid_val, VAL_NONE);
      lv_obj_set_style_text_color(s_pmkid_val, lv_color_hex(COL_DIM), 0);
    }
  }
}

static void poll_worker(void *arg) {
  (void)arg;
  wifi_service_start();
  wifi_sniffer_clear_handshake();
  wifi_sniffer_clear_pmkid();
  wifi_sniffer_start_monitor(MONITOR_CHANNEL);

  while (s_is_poll_running) {
    uint32_t flags = 0;
    if (wifi_sniffer_handshake_captured()) {
      flags |= FLAG_HANDSHAKE;
      wifi_sniffer_get_handshake_bssid(s_hs_bssid);
    }
    if (wifi_sniffer_pmkid_captured()) {
      flags |= FLAG_PMKID;
      wifi_sniffer_get_pmkid_bssid(s_pm_bssid);
    }
    lv_async_call(state_ready_cb, (void *)(uintptr_t)flags);
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }

  wifi_sniffer_stop();
  s_is_worker_busy = false;
  vTaskDelete(NULL);
}

static void stop_detection(void) {
  s_is_poll_running = false;
}

static lv_obj_t *make_row(lv_obj_t *parent, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, LV_PCT(100), h);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

static lv_obj_t *
make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

static void build_info_card(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, LV_PCT(100), CARD1_H);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_hor(card, CARD1_PAD_H, 0);
  lv_obj_set_style_pad_ver(card, CARD1_PAD_V, 0);

  lv_obj_t *ic =
      make_label(card, LV_SYMBOL_WIFI, &lv_font_montserrat_14, current_theme.border_accent);
  lv_obj_align(ic, LV_ALIGN_TOP_LEFT, ICON_X, ROW1_Y);

  lv_obj_t *name = make_label(card, AP_NAME, &lv_font_montserrat_14, current_theme.text_main);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, NAME_X, ROW1_Y);

  lv_obj_t *chip = lv_obj_create(card);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, CHIP_H);
  lv_obj_align(chip, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_radius(chip, BADGE_RADIUS, 0);
  lv_obj_set_style_pad_hor(chip, CHIP_PAD_H, 0);
  lv_obj_set_style_pad_ver(chip, 0, 0);
  lv_obj_set_style_bg_color(chip, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
  lv_obj_set_style_border_color(chip, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_t *chip_l = make_label(chip, AP_CHAN, &lv_font_montserrat_12, current_theme.border_accent);
  lv_obj_center(chip_l);

  lv_obj_t *b_tag = make_label(card, LBL_BSSID, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_align(b_tag, LV_ALIGN_TOP_LEFT, 0, BSSID_Y);
  s_bssid_val = make_label(card, BSSID_NONE, &lv_font_montserrat_12, current_theme.text_main);
  lv_obj_align(s_bssid_val, LV_ALIGN_TOP_RIGHT, 0, BSSID_Y);

  lv_obj_t *s_tag = make_label(card, LBL_STA, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_align(s_tag, LV_ALIGN_TOP_LEFT, 0, STA_Y);
  lv_obj_t *s_val = make_label(card, VAL_NONE, &lv_font_montserrat_12, current_theme.text_main);
  lv_obj_align(s_val, LV_ALIGN_TOP_RIGHT, 0, STA_Y);
}

static void build_eapol_card(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, LV_PCT(100), CARD2_H);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, CARD2_PAD, 0);

  lv_obj_t *lbl = make_label(card, LBL_EAPOL, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, EAPOL_LBL_Y);

  s_status = make_label(card, TXT_CAPTURING, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, 0, EAPOL_LBL_Y);

  lv_obj_t *wave = lv_line_create(card);
  lv_obj_set_size(wave, WAVE_W, WAVE_H);
  lv_obj_align(wave, LV_ALIGN_TOP_LEFT, WAVE_X, WAVE_Y);
  lv_line_set_points(wave, WAVE_PTS, WAVE_PT_COUNT);
  lv_obj_set_style_line_width(wave, WAVE_LINE_W, 0);
  lv_obj_set_style_line_color(wave, current_theme.border_accent, 0);
  lv_obj_set_style_line_rounded(wave, true, 0);

  lv_obj_t *badges = lv_obj_create(card);
  lv_obj_remove_flag(badges, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(badges, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(badges, LV_PCT(100), BADGE_ROW_H);
  lv_obj_align(badges, LV_ALIGN_TOP_LEFT, 0, BADGE_ROW_Y);
  lv_obj_set_style_bg_opa(badges, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(badges, 0, 0);
  lv_obj_set_style_pad_all(badges, 0, 0);
  lv_obj_set_flex_flow(badges, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(badges, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(badges, BADGE_GAP, 0);

  static const char *BADGE_TXT[BADGE_COUNT] = {
      "M1 " LV_SYMBOL_OK, "M2 " LV_SYMBOL_OK, "M3 " LV_SYMBOL_OK, "M4 " LV_SYMBOL_OK};
  for (int i = 0; i < BADGE_COUNT; i++) {
    lv_obj_t *b = lv_obj_create(badges);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(b, BADGE_ROW_H);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, BADGE_RADIUS, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t *bl = make_label(b, BADGE_TXT[i], &lv_font_montserrat_12, lv_color_hex(COL_DIM));
    lv_obj_center(bl);
    s_badge[i] = bl;
    s_badge_box[i] = b;
  }
}

static void build_pmkid_row(lv_obj_t *parent) {
  lv_obj_t *row = make_row(parent, PMKID_ROW_H);
  lv_obj_t *tag = make_label(row, LBL_PMKID, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_align(tag, LV_ALIGN_LEFT_MID, 0, 0);
  s_pmkid_val = make_label(row, VAL_NONE, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_align(s_pmkid_val, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void build_saved_row(lv_obj_t *parent) {
  lv_obj_t *row = make_row(parent, SAVED_ROW_H);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, SAVED_GAP, 0);

  s_saved_dot = lv_obj_create(row);
  lv_obj_remove_flag(s_saved_dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_saved_dot, DOT_SIZE, DOT_SIZE);
  lv_obj_set_style_radius(s_saved_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_saved_dot, 0, 0);
  lv_obj_set_style_bg_opa(s_saved_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_saved_dot, lv_color_hex(COL_DIM), 0);

  s_saved_txt = make_label(row, TXT_CAPTURING, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
}

static void wifi_handshake_input(const input_event_t *ev, void *ctx) {
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
    case INPUT_BTN_OK:
      if (press)
        ui_feedback(UI_FB_WRITE);
      break;
    default:
      break;
  }
}

void ui_wifi_handshake_open(void) {
  stop_detection();

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status = NULL;
  s_saved_dot = NULL;
  s_saved_txt = NULL;
  s_bssid_val = NULL;
  s_pmkid_val = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);
  ui_chrome_footer(s_screen, FOOTER_HINT);

  lv_obj_t *stack = lv_obj_create(s_screen);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(stack, CONTENT_W, BODY_H);
  lv_obj_align(stack, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(stack, 0, 0);
  lv_obj_set_style_pad_all(stack, 0, 0);
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(stack, STACK_GAP, 0);

  build_info_card(stack);
  build_eapol_card(stack);
  build_pmkid_row(stack);
  build_saved_row(stack);

  set_progress(0);

  ui_input_set_screen_handler(wifi_handshake_input, NULL);

  s_is_poll_running = true;
  if (!s_is_worker_busy) {
    s_is_worker_busy = true;
    if (xTaskCreatePinnedToCore(
            poll_worker, "hs_poll", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_is_worker_busy = false;
      s_is_poll_running = false;
      ESP_LOGW(TAG, "failed to spawn handshake worker");
    }
  }

  ui_screen_load_owned(&s_screen, s_screen);
}

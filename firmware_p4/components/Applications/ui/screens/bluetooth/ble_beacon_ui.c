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

#include "ble_beacon_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define SPAM_TICK_MS    120
#define BEACON_CYCLE_MS 400

#define BODY_W 240
#define BODY_H 256
#define CARD_W 160
#define CARD_H 54

typedef struct {
  const char *kind;
  const char *uuid;
} beacon_t;

static const beacon_t BEACONS[] = {
    {"iBeacon", "e2c56db5-dffb-...-cc0215"},
    {"Eddystone URL", "https://high.co/xY9"},
    {"Eddystone UID", "ed0102 03040506 07a1"},
    {"AltBeacon", "4f8a1b2c-9d21-...-8e04"},
    {"iBeacon", "b9407f30-f5f8-...-2415a1"},
    {"Eddystone UID", "aa0b3c 5566778899 c4d2"},
};
#define BEACONS_COUNT (sizeof(BEACONS) / sizeof(BEACONS[0]))

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_spam_timer = NULL;
static lv_timer_t *s_cycle_timer = NULL;

static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_uuid_label = NULL;

static int s_beacons = 0;
static int s_beacon_idx = 0;

static void beacon_input(const input_event_t *ev, void *ctx);
static void spam_tick_cb(lv_timer_t *timer);
static void cycle_tick_cb(lv_timer_t *timer);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void run_stop_timers(void) {
  if (s_spam_timer != NULL) {
    lv_timer_delete(s_spam_timer);
    s_spam_timer = NULL;
  }
  if (s_cycle_timer != NULL) {
    lv_timer_delete(s_cycle_timer);
    s_cycle_timer = NULL;
  }
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
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);
  return card;
}

void ui_beacon_spam_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_beacons = 0;
  s_beacon_idx = 0;
  s_spam_timer = NULL;
  s_cycle_timer = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *header = ui_chrome_header(s_screen, "BEACON SPAM", "/assets/icons/sensors.bin");
  ui_chrome_footer(s_screen, "BACK Stop");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_set_size(body, BODY_W, BODY_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_radius(body, 0, 0);
  lv_obj_set_style_pad_all(body, 10, 0);

  lv_obj_t *status = lv_label_create(body);
  lv_label_set_text(status, "Broadcasting...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 54);

  lv_obj_t *type = lv_label_create(body);
  lv_label_set_text(type, "Type: Mixed");
  lv_obj_set_style_text_color(type, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(type, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(type, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(type, LV_ALIGN_TOP_MID, 0, 78);

  lv_obj_t *card = lit_card(body, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 100);

  s_count_label = lv_label_create(card);
  lv_label_set_text(s_count_label, "Beacons: 0");
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(s_count_label);

  s_uuid_label = lv_label_create(body);
  lv_label_set_text_fmt(s_uuid_label, "%s  %s", BEACONS[0].kind, BEACONS[0].uuid);
  lv_obj_set_style_text_color(s_uuid_label, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(s_uuid_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_uuid_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_uuid_label, LV_ALIGN_TOP_MID, 0, 166);

  fade_in(header, 200);
  fade_in(status, 200);
  fade_in(type, 240);
  fade_in(card, 260);
  fade_in(s_uuid_label, 300);

  ui_input_set_screen_handler(beacon_input, NULL);
  s_spam_timer = lv_timer_create(spam_tick_cb, SPAM_TICK_MS, NULL);
  s_cycle_timer = lv_timer_create(cycle_tick_cb, BEACON_CYCLE_MS, NULL);

  ui_feedback(UI_FB_EMULATE);
  notify(NOTIFY_INFO, "Beacon spam started");

  ui_screen_load_owned(&s_screen, s_screen);
}

static void spam_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_spam_timer == timer)
      s_spam_timer = NULL;
    return;
  }

  s_beacons++;
  if (s_count_label != NULL)
    lv_label_set_text_fmt(s_count_label, "Beacons: %d", s_beacons);
}

static void cycle_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_cycle_timer == timer)
      s_cycle_timer = NULL;
    return;
  }

  s_beacon_idx = (s_beacon_idx + 1) % (int)BEACONS_COUNT;
  if (s_uuid_label != NULL)
    lv_label_set_text_fmt(
        s_uuid_label, "%s  %s", BEACONS[s_beacon_idx].kind, BEACONS[s_beacon_idx].uuid);
}

static void beacon_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        run_stop_timers();
        ui_switch_screen(SCREEN_BLE_MENU);
      }
      break;
    default:
      break;
  }
}

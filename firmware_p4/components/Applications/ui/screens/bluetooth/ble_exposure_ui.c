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

#include "ble_exposure_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"
#include "lvgl.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "BLE_EXPOSURE_UI";

#define FEED_TICK_MS          700

#define MAX_ADV        6
#define MAC_LEN        18
#define ROW_LEN        36
#define LIST_BUF_LEN   (MAX_ADV * ROW_LEN)
#define MAX_AGE_TICKS  11
#define ADD_CHANCE_PCT 55
#define SEED_COUNT     3

#define RSSI_MIN  48
#define RSSI_SPAN 48
#define PCT_MOD   100

#define COL_DIM     0x8A8594
#define COL_SUCCESS 0x00E676

#define EXPO_ICON "/assets/icons/broadcast_on_personal.bin"

#define COUNT_CARD_W 190
#define COUNT_CARD_H 34
#define COUNT_CARD_Y 50
#define LIST_PANEL_W 224
#define LIST_PANEL_H 190
#define LIST_PANEL_Y 94
#define LIST_PAD     8

typedef struct {
  char mac[MAC_LEN];
  int rssi;
  uint16_t age;
  bool used;
} adv_t;

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_feed_timer = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_list_label = NULL;

static adv_t s_adv[MAX_ADV];

static void exposure_input(const input_event_t *ev, void *ctx);
static void feed_tick_cb(lv_timer_t *timer);

static void stop_feed_timer(void) {
  if (s_feed_timer != NULL) {
    lv_timer_delete(s_feed_timer);
    s_feed_timer = NULL;
  }
}

static void gen_mac(char *out, size_t n) {
  snprintf(out,
           n,
           "%02X:%02X:%02X:%02X:%02X:%02X",
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF));
}

static void add_advertiser(void) {
  for (int i = 0; i < MAX_ADV; i++) {
    if (!s_adv[i].used) {
      gen_mac(s_adv[i].mac, sizeof(s_adv[i].mac));
      s_adv[i].rssi = -(int)(RSSI_MIN + (esp_random() % RSSI_SPAN));
      s_adv[i].age = 0;
      s_adv[i].used = true;
      return;
    }
  }
}

static int active_count(void) {
  int c = 0;
  for (int i = 0; i < MAX_ADV; i++)
    if (s_adv[i].used)
      c++;
  return c;
}

static void render_list(void) {
  if (s_list_label != NULL) {
    char buf[LIST_BUF_LEN];
    size_t pos = 0;
    bool first = true;
    for (int i = 0; i < MAX_ADV && pos < sizeof(buf); i++) {
      if (!s_adv[i].used)
        continue;
      int secs = (int)(s_adv[i].age * FEED_TICK_MS / 1000);
      int m = snprintf(buf + pos,
                       sizeof(buf) - pos,
                       first ? "%s  %d  %ds" : "\n%s  %d  %ds",
                       s_adv[i].mac,
                       s_adv[i].rssi,
                       secs);
      if (m < 0)
        break;
      pos += (size_t)m;
      first = false;
    }
    if (first)
      snprintf(buf, sizeof(buf), "No advertisers");
    lv_label_set_text(s_list_label, buf);
  }

  if (s_count_label != NULL)
    lv_label_set_text_fmt(s_count_label, "Advertisers: %d", active_count());
}

void ui_ble_exposure_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_feed_timer = NULL;
  s_count_label = NULL;
  s_list_label = NULL;

  for (int i = 0; i < MAX_ADV; i++)
    s_adv[i].used = false;
  for (int i = 0; i < SEED_COUNT; i++)
    add_advertiser();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "EXPOSURE", EXPO_ICON);
  ui_chrome_footer(s_screen, "BACK Back");

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_set_size(card, COUNT_CARD_W, COUNT_CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, COUNT_CARD_Y);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);

  s_count_label = lv_label_create(card);
  lv_label_set_text(s_count_label, "Advertisers: 0");
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_center(s_count_label);

  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_set_size(panel, LIST_PANEL_W, LIST_PANEL_H);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, LIST_PANEL_Y);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(panel, 10, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(panel, LIST_PAD, 0);

  s_list_label = lv_label_create(panel);
  lv_obj_set_style_text_color(s_list_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_list_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_list_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(s_list_label, LV_ALIGN_TOP_LEFT, 0, 0);

  render_list();

  ui_input_set_screen_handler(exposure_input, NULL);
  s_feed_timer = lv_timer_create(feed_tick_cb, FEED_TICK_MS, NULL);

  ui_feedback(UI_FB_SELECT);
  ui_screen_load(s_screen);
  ESP_LOGI(TAG, "BLE exposure screen opened (mock GAEN)");
}

static void feed_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_feed_timer == timer)
      s_feed_timer = NULL;
    return;
  }

  for (int i = 0; i < MAX_ADV; i++) {
    if (s_adv[i].used) {
      s_adv[i].age++;
      if (s_adv[i].age > MAX_AGE_TICKS)
        s_adv[i].used = false;
    }
  }

  if ((esp_random() % PCT_MOD) < ADD_CHANCE_PCT)
    add_advertiser();

  render_list();
}

static void exposure_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        stop_feed_timer();
        ui_switch_screen(SCREEN_BLE_DETECT_MENU);
      }
      break;
    default:
      break;
  }
}

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

#include "ble_spam_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "st7789.h"

#include "canned_spam.h"
#include "intensity_bar_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static const char *TAG = "BLE_SPAM_UI";

#define RUN_TICK_MS 250
#define SPAM_ICON   "/assets/icons/broadcast_on_personal.bin"

#define SPAM_GRID_PAD      8
#define SPAM_GRID_GAP      6
#define SPAM_CARD_W        109
#define SPAM_CARD_H        76
#define SPAM_CARD_PAD      6
#define SPAM_CARD_ROW_GAP  3
#define SPAM_CARD_GLOW_ON  22
#define SPAM_CARD_GLOW_OFF 16
#define SPAM_EDIT_ICON_HEX 0xB89AFF

static const uint32_t SPAM_ECO_COLOR[] = {
    0xE0E0E0,
    0xFF8A5B,
    0x4CBDD6,
    0x54D08A,
    0x54D08A,
    0xFFC24C,
};
#define SPAM_ECO_COLOR_COUNT (sizeof(SPAM_ECO_COLOR) / sizeof(SPAM_ECO_COLOR[0]))

#define SPAM_MAX_ATTACKS 12
#define SPAM_CARD_MAX    (SPAM_MAX_ATTACKS + 1)

static int s_attack_count = 0;
static int s_custom_idx = 0;
static int s_spam_mode = 0;

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, 13, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, 18, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  lv_obj_set_style_pad_all(p, 0, 0);
  return p;
}

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static lv_obj_t *s_select_screen = NULL;
static lv_obj_t *s_spam_cards[SPAM_CARD_MAX];
static int s_card_count = 0;
static int s_sel_idx = 0;

static void ble_spam_select_input(const input_event_t *ev, void *ctx);

static lv_obj_t *build_spam_card(
    lv_obj_t *parent, const char *eco, uint32_t eco_hex, const char *title, const char *effect) {
  lv_obj_t *c = lit_panel(parent, SPAM_CARD_W, SPAM_CARD_H);
  lv_obj_set_style_pad_all(c, SPAM_CARD_PAD, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(c, SPAM_CARD_ROW_GAP, 0);

  lv_obj_t *e = lv_label_create(c);
  lv_label_set_text(e, eco);
  lv_obj_set_style_text_font(e, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(e, lv_color_hex(eco_hex), 0);

  lv_obj_t *t = lv_label_create(c);
  lv_obj_set_width(t, SPAM_CARD_W - 2 * SPAM_CARD_PAD);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);

  lv_obj_t *d = lv_label_create(c);
  lv_label_set_text(d, effect);
  lv_obj_set_style_text_font(d, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(d, current_theme.text_secondary, 0);
  return c;
}

static void select_card(int idx) {
  if (idx < 0 || idx >= s_card_count)
    return;
  for (int i = 0; i < s_card_count; i++) {
    if (s_spam_cards[i] == NULL)
      continue;
    bool on = (i == idx);
    lv_obj_set_style_border_width(s_spam_cards[i], on ? 2 : 1, 0);
    lv_obj_set_style_shadow_width(s_spam_cards[i], on ? SPAM_CARD_GLOW_ON : SPAM_CARD_GLOW_OFF, 0);
    lv_obj_set_style_shadow_opa(s_spam_cards[i], on ? LV_OPA_70 : LV_OPA_30, 0);
  }
  s_sel_idx = idx;
}

void ui_ble_spam_select_open(void) {
  if (s_select_screen != NULL) {
    lv_obj_del(s_select_screen);
    s_select_screen = NULL;
  }

  s_attack_count = spam_get_attack_count();
  if (s_attack_count > SPAM_MAX_ATTACKS)
    s_attack_count = SPAM_MAX_ATTACKS;
  s_custom_idx = s_attack_count;
  s_card_count = s_attack_count + 1;

  for (int i = 0; i < SPAM_CARD_MAX; i++)
    s_spam_cards[i] = NULL;

  s_select_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_select_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_select_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_select_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_select_screen, 0, 0);
  lv_obj_set_style_pad_all(s_select_screen, 0, 0);

  ui_chrome_header(s_select_screen, "DEVICE SPAM", SPAM_ICON);
  ui_chrome_footer(s_select_screen, "OK Start   BACK Back");

  lv_obj_t *grid = lv_obj_create(s_select_screen);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grid, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, SPAM_GRID_PAD, 0);
  lv_obj_set_style_pad_row(grid, SPAM_GRID_GAP, 0);
  lv_obj_set_style_pad_column(grid, SPAM_GRID_GAP, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  for (int i = 0; i < s_attack_count; i++) {
    const canned_spam_type_t *t = spam_get_attack_type(i);
    const char *name = (t != NULL && t->name != NULL) ? t->name : "Attack";
    uint32_t color = SPAM_ECO_COLOR[i % SPAM_ECO_COLOR_COUNT];
    s_spam_cards[i] = build_spam_card(grid, "BLE", color, name, "advertise");
  }
  s_spam_cards[s_custom_idx] =
      build_spam_card(grid, "EDIT", SPAM_EDIT_ICON_HEX, "Custom Names", "your list");

  s_sel_idx = 0;
  select_card(0);

  ui_input_set_screen_handler(ble_spam_select_input, NULL);

  ui_screen_load_owned(&s_select_screen, s_select_screen);
  fade_in(grid, 240);
}

static void ble_spam_select_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) {
        select_card((s_sel_idx + 1) % s_card_count);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        select_card((s_sel_idx - 1 + s_card_count) % s_card_count);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_BLE_MENU);
      break;
    case INPUT_BTN_OK:
      if (press) {
        int sel = s_sel_idx;
        ui_feedback(UI_FB_SELECT);
        if (sel == s_custom_idx) {
          ui_switch_screen(SCREEN_BLE_SPAM_NAMES);
        } else {
          if (sel >= 0 && sel < s_attack_count)
            s_spam_mode = sel;
          ui_switch_screen(SCREEN_BLE_SPAM);
        }
      }
      break;
    default:
      break;
  }
}

static lv_obj_t *s_run_screen = NULL;
static lv_obj_t *s_run_hint = NULL;
static lv_timer_t *s_run_spam_timer = NULL;
static lv_obj_t *s_run_count_label = NULL;
static lv_obj_t *s_run_rate_label = NULL;
static intensity_bar_t s_run_intensity;
static int64_t s_run_start_us = 0;
static bool s_is_run_active = false;

static void ble_spam_run_input(const input_event_t *ev, void *ctx);
static void run_spam_tick_cb(lv_timer_t *timer);

static void run_stop(void) {
  if (s_run_spam_timer != NULL) {
    lv_timer_delete(s_run_spam_timer);
    s_run_spam_timer = NULL;
  }
  if (s_is_run_active) {
    spam_stop();
    s_is_run_active = false;
  }
}

void ui_ble_spam_open(void) {
  if (s_run_screen != NULL) {
    lv_obj_del(s_run_screen);
    s_run_screen = NULL;
  }
  s_run_spam_timer = NULL;
  s_is_run_active = false;
  s_run_start_us = esp_timer_get_time();

  s_run_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_run_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_run_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_run_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_run_screen, 0, 0);
  lv_obj_set_style_pad_all(s_run_screen, 0, 0);

  ui_chrome_header(s_run_screen, "DEVICE SPAM", SPAM_ICON);
  s_run_hint = ui_chrome_footer(s_run_screen, "BACK  Stop");

  lv_obj_t *body = lv_obj_create(s_run_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(body, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);

  lv_obj_t *status = lv_label_create(body);
  lv_label_set_text(status, "Spamming...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 8);

  const canned_spam_type_t *mt = spam_get_attack_type(s_spam_mode);
  lv_obj_t *mode = lv_label_create(body);
  lv_label_set_text_fmt(mode, "Mode: %s", (mt != NULL && mt->name) ? mt->name : "?");
  lv_obj_set_style_text_color(mode, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(mode, &lv_font_montserrat_12, 0);
  lv_obj_align(mode, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t *card = lit_panel(body, 168, 50);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 64);
  s_run_count_label = lv_label_create(card);
  lv_label_set_text(s_run_count_label, "00:00");
  lv_obj_set_style_text_color(s_run_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_run_count_label, &lv_font_montserrat_16, 0);
  lv_obj_center(s_run_count_label);

  intensity_bar_create(&s_run_intensity, body);
  lv_obj_align(s_run_intensity.obj, LV_ALIGN_TOP_MID, 0, 138);
  intensity_bar_set(&s_run_intensity, 5);

  s_run_rate_label = lv_label_create(body);
  lv_label_set_text(s_run_rate_label, "Broadcasting");
  lv_obj_set_style_text_color(s_run_rate_label, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(s_run_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_run_rate_label, LV_ALIGN_TOP_MID, 0, 180);

  esp_err_t err = spam_start(s_spam_mode);
  if (err == ESP_OK) {
    s_is_run_active = true;
    s_run_start_us = esp_timer_get_time();
  } else {
    lv_label_set_text(status, "Spam unavailable");
    lv_label_set_text(s_run_rate_label, "Radio not running");
    intensity_bar_set(&s_run_intensity, 0);
    ESP_LOGE(TAG, "spam_start(%d) failed: %s", s_spam_mode, esp_err_to_name(err));
  }

  fade_in(status, 200);
  fade_in(mode, 240);
  fade_in(card, 280);
  fade_in(s_run_intensity.obj, 320);
  fade_in(s_run_rate_label, 340);

  ui_feedback(UI_FB_EMULATE);

  ui_input_set_screen_handler(ble_spam_run_input, NULL);
  if (s_is_run_active)
    s_run_spam_timer = lv_timer_create(run_spam_tick_cb, RUN_TICK_MS, NULL);

  ui_screen_load_owned(&s_run_screen, s_run_screen);
}

static void run_spam_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_run_screen) {
    run_stop();
    return;
  }
  if (s_run_count_label == NULL)
    return;
  int secs = (int)((esp_timer_get_time() - s_run_start_us) / 1000000);
  lv_label_set_text_fmt(s_run_count_label, "%02d:%02d", secs / 60, secs % 60);
}

static void ble_spam_run_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        run_stop();
        ui_switch_screen(SCREEN_BLE_SPAM_SELECT);
      }
      break;
    default:
      break;
  }
}

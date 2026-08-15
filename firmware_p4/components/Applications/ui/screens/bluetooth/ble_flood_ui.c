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

#include "ble_flood_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"
#include "st7789.h"

#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define FLOOD_TICK_MS 100
#define TICKS_PER_SEC (1000 / FLOOD_TICK_MS)

#define FLOOD_ICON "/assets/icons/broadcast_on_personal.bin"

#define COL_DIM 0x8A8594

#define CARD_W 172
#define CARD_H 54

static const char *const FLOOD_MODES[] = {
    "Connect flood",
    "L2CAP flood",
};
#define FLOOD_MODES_COUNT ((int)(sizeof(FLOOD_MODES) / sizeof(FLOOD_MODES[0])))

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_rate_label = NULL;
static lv_timer_t *s_flood_timer = NULL;

static char s_target[40];
static int s_mode = 0;
static int s_sent = 0;
static int s_sent_prev = 0;
static int s_tick_accum = 0;

static void ble_flood_input(const input_event_t *ev, void *ctx);
static void flood_tick_cb(lv_timer_t *timer);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void stop_flood_timer(void) {
  if (s_flood_timer != NULL) {
    lv_timer_delete(s_flood_timer);
    s_flood_timer = NULL;
  }
}

static lv_obj_t *lit_card(lv_obj_t *parent, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 20, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  return card;
}

static void set_mode_text(void) {
  if (s_mode_label != NULL)
    lv_label_set_text_fmt(
        s_mode_label, LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, FLOOD_MODES[s_mode]);
}

void ui_ble_flood_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_mode = 0;
  s_sent = 0;
  s_sent_prev = 0;
  s_tick_accum = 0;
  s_flood_timer = NULL;

  snprintf(s_target,
           sizeof(s_target),
           "Target-PC (AA:BB:%02X:%02X)",
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF));

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "BLE FLOOD", FLOOD_ICON);
  ui_chrome_footer(s_screen, "L/R Mode   BACK Stop");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(body, LCD_H_RES, LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);

  lv_obj_t *status = lv_label_create(body);
  lv_label_set_text(status, "Flooding...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *target = lv_label_create(body);
  lv_label_set_text(target, s_target);
  lv_obj_set_style_text_color(target, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(target, &lv_font_montserrat_12, 0);
  lv_obj_align(target, LV_ALIGN_TOP_MID, 0, 38);

  s_mode_label = lv_label_create(body);
  lv_obj_set_style_text_color(s_mode_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_mode_label, &lv_font_montserrat_14, 0);
  lv_obj_align(s_mode_label, LV_ALIGN_TOP_MID, 0, 72);
  set_mode_text();

  lv_obj_t *card = lit_card(body, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 108);

  s_count_label = lv_label_create(card);
  lv_label_set_text(s_count_label, "Sent: 0");
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_center(s_count_label);

  s_rate_label = lv_label_create(body);
  lv_label_set_text(s_rate_label, "Rate: 0/s");
  lv_obj_set_style_text_color(s_rate_label, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(s_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_rate_label, LV_ALIGN_TOP_MID, 0, 178);

  fade_in(status, 200);
  fade_in(target, 240);
  fade_in(s_mode_label, 280);
  fade_in(card, 320);
  fade_in(s_rate_label, 340);

  s_flood_timer = lv_timer_create(flood_tick_cb, FLOOD_TICK_MS, NULL);

  ui_input_set_screen_handler(ble_flood_input, NULL);

  ui_feedback(UI_FB_EMULATE);
  notify(NOTIFY_INFO, "Flood started");
  ui_screen_load(s_screen);
}

static void flood_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_flood_timer == timer)
      s_flood_timer = NULL;
    return;
  }

  s_sent += 1 + (int)(esp_random() % 4);
  if (s_count_label != NULL)
    lv_label_set_text_fmt(s_count_label, "Sent: %d", s_sent);

  s_tick_accum++;
  if (s_tick_accum >= TICKS_PER_SEC && s_rate_label != NULL) {
    int rate = s_sent - s_sent_prev;
    lv_label_set_text_fmt(s_rate_label, "Rate: %d/s", rate);
    s_sent_prev = s_sent;
    s_tick_accum = 0;
  }
}

static void ble_flood_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        stop_flood_timer();
        ui_switch_screen(SCREEN_BLE_MENU);
      }
      break;
    case INPUT_BTN_RIGHT:
      if (nav) {
        s_mode = (s_mode + 1) % FLOOD_MODES_COUNT;
        set_mode_text();
        fade_in(s_mode_label, 160);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_LEFT:
      if (nav) {
        s_mode = (s_mode - 1 + FLOOD_MODES_COUNT) % FLOOD_MODES_COUNT;
        set_mode_text();
        fade_in(s_mode_label, 160);
        ui_feedback(UI_FB_NAV);
      }
      break;
    default:
      break;
  }
}

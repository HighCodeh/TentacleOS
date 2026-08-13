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

#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "intensity_bar_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_INTERVAL_MS 50
#define SPAM_TICK_MS          120
#define TARGET_CYCLE_MS       500
#define COL_DIM               0x8A8594
#define SPAM_ICON             "/assets/icons/broadcast_on_personal.bin"

#define SPAM_GRID_PAD      8
#define SPAM_GRID_GAP      6
#define SPAM_CARD_W        109
#define SPAM_CARD_H        76
#define SPAM_CARD_PAD      6
#define SPAM_CARD_ROW_GAP  3
#define SPAM_CARD_GLOW_ON  22
#define SPAM_CARD_GLOW_OFF 16
#define SPAM_EDIT_ICON_HEX 0xB89AFF

static const char *const SPAM_ITEMS[] = {
    "Apple Juice",
    "SourApple",
    "Android",
    "Windows Swift",
    "All",
};
#define SPAM_ITEMS_COUNT (sizeof(SPAM_ITEMS) / sizeof(SPAM_ITEMS[0]))

static const char *const SPAM_TARGETS[] = {
    "iPhone 14",
    "AirPods Pro",
    "Galaxy Buds",
    "MacBook",
    "Mi Band",
};
#define SPAM_TARGETS_COUNT (sizeof(SPAM_TARGETS) / sizeof(SPAM_TARGETS[0]))

static const char *const SPAM_ECOSYSTEM[] = {
    "APPLE",
    "SOUR",
    "ANDROID",
    "WINDOWS",
    "ALL",
};

static const uint32_t SPAM_ECO_COLOR[] = {
    0xE0E0E0,
    0xFF8A5B,
    0x54D08A,
    0x4CBDD6,
    0xB89AFF,
};

static const char *const SPAM_EFFECT[] = {
    "popup flood",
    "iOS crash",
    "pairing spam",
    "notif flood",
    "everything",
};

#define SPAM_CUSTOM_IDX SPAM_ITEMS_COUNT
#define SPAM_CARD_COUNT (SPAM_ITEMS_COUNT + 1)

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
static lv_obj_t *s_spam_cards[SPAM_CARD_COUNT];
static int s_sel_idx = 0;
static lv_timer_t *s_select_nav_timer = NULL;

static bool s_sel_up_last = false;
static bool s_sel_down_last = false;
static bool s_sel_ok_last = false;
static bool s_sel_back_last = false;
static bool s_sel_left_last = false;

static void select_nav_timer_cb(lv_timer_t *timer);

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
  lv_obj_set_style_text_color(d, lv_color_hex(COL_DIM), 0);
  return c;
}

static void select_card(int idx) {
  if (idx < 0 || idx >= (int)SPAM_CARD_COUNT)
    return;
  for (int i = 0; i < (int)SPAM_CARD_COUNT; i++) {
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
  for (int i = 0; i < (int)SPAM_CARD_COUNT; i++)
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
  lv_obj_set_size(grid, LCD_H_RES, LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, SPAM_GRID_PAD, 0);
  lv_obj_set_style_pad_row(grid, SPAM_GRID_GAP, 0);
  lv_obj_set_style_pad_column(grid, SPAM_GRID_GAP, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  for (size_t i = 0; i < SPAM_ITEMS_COUNT; i++)
    s_spam_cards[i] =
        build_spam_card(grid, SPAM_ECOSYSTEM[i], SPAM_ECO_COLOR[i], SPAM_ITEMS[i], SPAM_EFFECT[i]);
  s_spam_cards[SPAM_CUSTOM_IDX] =
      build_spam_card(grid, "EDIT", SPAM_EDIT_ICON_HEX, "Custom Names", "your list");

  s_sel_idx = 0;
  select_card(0);

  s_sel_up_last = false;
  s_sel_down_last = false;
  s_sel_ok_last = false;
  s_sel_back_last = false;
  s_sel_left_last = false;

  if (s_select_nav_timer == NULL)
    s_select_nav_timer = lv_timer_create(select_nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load(s_select_screen);
  fade_in(grid, 240);
}

static void select_nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_select_screen) {
    lv_timer_delete(timer);
    s_select_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool is_up = ui_btn_up();
  bool is_down = ui_btn_down();
  bool is_ok = ok_button_is_down();
  bool is_back = back_button_is_down();
  bool is_left = left_button_is_down();

  if (is_down && !s_sel_down_last) {
    select_card((s_sel_idx + 1) % (int)SPAM_CARD_COUNT);
    ui_feedback(UI_FB_NAV);
  }
  if (is_up && !s_sel_up_last) {
    select_card((s_sel_idx - 1 + (int)SPAM_CARD_COUNT) % (int)SPAM_CARD_COUNT);
    ui_feedback(UI_FB_NAV);
  }

  if ((is_back && !s_sel_back_last) || (is_left && !s_sel_left_last))
    ui_switch_screen(SCREEN_BLE_MENU);

  if (is_ok && !s_sel_ok_last) {
    int sel = s_sel_idx;
    ui_feedback(UI_FB_SELECT);
    if (sel == (int)SPAM_CUSTOM_IDX) {
      ui_switch_screen(SCREEN_BLE_SPAM_NAMES);
    } else {
      if (sel >= 0 && sel < (int)SPAM_ITEMS_COUNT)
        s_spam_mode = sel;
      ui_switch_screen(SCREEN_BLE_SPAM);
    }
  }

  s_sel_up_last = is_up;
  s_sel_down_last = is_down;
  s_sel_ok_last = is_ok;
  s_sel_back_last = is_back;
  s_sel_left_last = is_left;
}

static lv_obj_t *s_run_screen = NULL;
static lv_obj_t *s_run_hint = NULL;
static lv_timer_t *s_run_nav_timer = NULL;
static lv_timer_t *s_run_spam_timer = NULL;
static lv_timer_t *s_run_target_timer = NULL;
static lv_obj_t *s_run_count_label = NULL;
static lv_obj_t *s_run_rate_label = NULL;
static lv_obj_t *s_run_target_label = NULL;
static intensity_bar_t s_run_intensity;
static int s_run_sent = 0;
static int s_run_sent_prev = 0;
static int s_run_tick_accum = 0;
static int s_run_target_idx = 0;

static bool s_run_back_last = false;

static void run_nav_timer_cb(lv_timer_t *timer);
static void run_spam_tick_cb(lv_timer_t *timer);
static void run_target_cycle_cb(lv_timer_t *timer);

static void run_stop_timers(void) {
  if (s_run_spam_timer != NULL) {
    lv_timer_delete(s_run_spam_timer);
    s_run_spam_timer = NULL;
  }
  if (s_run_target_timer != NULL) {
    lv_timer_delete(s_run_target_timer);
    s_run_target_timer = NULL;
  }
}

static int rate_to_level(int per_sec) {
  if (per_sec <= 0)
    return 0;
  if (per_sec < 3)
    return 1;
  if (per_sec < 5)
    return 2;
  if (per_sec < 7)
    return 3;
  if (per_sec < 9)
    return 4;
  return 5;
}

void ui_ble_spam_open(void) {
  if (s_run_screen != NULL) {
    lv_obj_del(s_run_screen);
    s_run_screen = NULL;
  }
  s_run_sent = 0;
  s_run_sent_prev = 0;
  s_run_tick_accum = 0;
  s_run_target_idx = 0;
  s_run_spam_timer = NULL;
  s_run_target_timer = NULL;
  s_run_back_last = false;

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
  lv_obj_set_size(body, LCD_H_RES, LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);

  lv_obj_t *status = lv_label_create(body);
  lv_label_set_text(status, "Spamming...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t *mode = lv_label_create(body);
  lv_label_set_text_fmt(mode, "Mode: %s", SPAM_ITEMS[s_spam_mode]);
  lv_obj_set_style_text_color(mode, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(mode, &lv_font_montserrat_12, 0);
  lv_obj_align(mode, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t *card = lit_panel(body, 168, 50);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 64);
  s_run_count_label = lv_label_create(card);
  lv_label_set_text(s_run_count_label, "Sent: 0");
  lv_obj_set_style_text_color(s_run_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_run_count_label, &lv_font_montserrat_16, 0);
  lv_obj_center(s_run_count_label);

  intensity_bar_create(&s_run_intensity, body);
  lv_obj_align(s_run_intensity.obj, LV_ALIGN_TOP_MID, 0, 138);
  intensity_bar_set(&s_run_intensity, 3);

  s_run_rate_label = lv_label_create(body);
  lv_label_set_text(s_run_rate_label, "Adv/s: 0");
  lv_obj_set_style_text_color(s_run_rate_label, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(s_run_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_run_rate_label, LV_ALIGN_TOP_MID, 0, 180);

  s_run_target_label = lv_label_create(body);
  lv_label_set_text(s_run_target_label, SPAM_TARGETS[0]);
  lv_obj_set_style_text_color(s_run_target_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_run_target_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_run_target_label, LV_ALIGN_TOP_MID, 0, 210);

  fade_in(status, 200);
  fade_in(mode, 240);
  fade_in(card, 280);
  fade_in(s_run_intensity.obj, 320);
  fade_in(s_run_rate_label, 340);

  ui_feedback(UI_FB_EMULATE);

  if (s_run_nav_timer == NULL)
    s_run_nav_timer = lv_timer_create(run_nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);
  s_run_spam_timer = lv_timer_create(run_spam_tick_cb, SPAM_TICK_MS, NULL);
  s_run_target_timer = lv_timer_create(run_target_cycle_cb, TARGET_CYCLE_MS, NULL);

  ui_screen_load(s_run_screen);
}

static void run_spam_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_run_screen) {
    lv_timer_delete(timer);
    if (s_run_spam_timer == timer)
      s_run_spam_timer = NULL;
    return;
  }

  s_run_sent++;
  lv_label_set_text_fmt(s_run_count_label, "Sent: %d", s_run_sent);

  s_run_tick_accum++;
  int ticks_per_sec = (1000 + SPAM_TICK_MS - 1) / SPAM_TICK_MS;
  if (s_run_tick_accum >= ticks_per_sec && s_run_rate_label != NULL) {
    int delta = s_run_sent - s_run_sent_prev;
    int per_sec = delta * 1000 / (s_run_tick_accum * SPAM_TICK_MS);
    lv_label_set_text_fmt(s_run_rate_label, "Adv/s: %d", per_sec);
    intensity_bar_set(&s_run_intensity, rate_to_level(per_sec));
    s_run_sent_prev = s_run_sent;
    s_run_tick_accum = 0;
  }
}

static void run_target_cycle_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_run_screen) {
    lv_timer_delete(timer);
    if (s_run_target_timer == timer)
      s_run_target_timer = NULL;
    return;
  }

  s_run_target_idx = (s_run_target_idx + 1) % (int)SPAM_TARGETS_COUNT;
  lv_label_set_text(s_run_target_label, SPAM_TARGETS[s_run_target_idx]);
  fade_in(s_run_target_label, 160);
}

static void run_nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_run_screen) {
    lv_timer_delete(timer);
    s_run_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool is_back = back_button_is_down();
  if (is_back && !s_run_back_last) {
    run_stop_timers();
    ui_switch_screen(SCREEN_BLE_SPAM_SELECT);
  }
  s_run_back_last = is_back;
}

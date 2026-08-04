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

#include "theme_selector_ui.h"

#include "esp_log.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "notify_ui.h"
#include "page_dots_ui.h"
#include "st7789.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "THEME_SELECTOR_UI";

#define NAV_TIMER_MS 50
#define TITLE_ICON   "/assets/icons/palette.bin"
#define BASE_FRAME   "/assets/frames/base_frame_0.bin"
#define CARD_Y_BIAS  (-18)

extern int theme_idx;

typedef struct {
  const char *label;
  uint32_t accent;
} theme_face_t;

static const theme_face_t THEMES[] = {
    {"Default", 0x834EC6},
    {"Matrix", 0x00FF41},
    {"Cyber Blue", 0x00D9FF},
    {"Blood", 0xFF0055},
    {"Toxic", 0xCCFF00},
    {"Ghost", 0xB8B8FF},
    {"Neon Pink", 0xFF00AA},
    {"Amber", 0xFFAA00},
    {"Terminal", 0x00FF00},
    {"Ice", 0x00FFFF},
    {"Deep Purple", 0xAA00FF},
    {"Midnight", 0x4488FF},
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

static const int32_t CAR_PX[] = {-94, -50, 0, 50, 94};
static const int32_t CAR_PY[] = {-14, -6, 0, -6, -14};
static const int32_t CAR_SC[] = {117, 161, 234, 161, 117};
static const int32_t CAR_OP[] = {LV_OPA_50, LV_OPA_80, LV_OPA_COVER, LV_OPA_80, LV_OPA_50};
static const int32_t CAR_Z[] = {0, 1, 2, 1, 0};
#define CAR_SLOTS  5
#define CAR_CENTER 2

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_cards[THEME_COUNT];
static lv_obj_t *s_label = NULL;
static lv_obj_t *s_active = NULL;
static page_dots_t s_dots;
static lv_image_dsc_t *s_base_dsc = NULL;
static lv_timer_t *s_nav_timer = NULL;
static int s_sel = 0;

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;

static void build_screen(void);

static int32_t carousel_slot(int item_idx) {
  int32_t n = THEME_COUNT;
  int32_t d = (item_idx - s_sel + n) % n;
  if (d > n / 2)
    d -= n;
  int32_t slot = CAR_CENTER + d;
  return (slot >= 0 && slot < CAR_SLOTS) ? slot : -1;
}

static lv_obj_t *make_card(lv_obj_t *parent, int i) {
  lv_obj_t *card = lv_image_create(parent);
  if (s_base_dsc)
    lv_image_set_src(card, s_base_dsc);
  lv_image_set_antialias(card, false);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
  lv_obj_set_style_image_recolor(card, lv_color_hex(THEMES[i].accent), 0);
  lv_obj_set_style_image_recolor_opa(card, LV_OPA_COVER, 0);
  return card;
}

static void place_card(int i) {
  lv_obj_t *card = s_cards[i];
  if (card == NULL)
    return;
  int32_t slot = carousel_slot(i);

  if (slot < 0) {
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);

  lv_obj_align(card, LV_ALIGN_CENTER, CAR_PX[slot], CAR_PY[slot] + CARD_Y_BIAS);
  lv_image_set_scale(card, CAR_SC[slot]);
  lv_obj_set_style_opa(card, CAR_OP[slot], 0);
}

static void fix_z_order(void) {
  for (int z = 0; z <= CAR_CENTER; z++) {
    for (int i = 0; i < THEME_COUNT; i++) {
      int32_t slot = carousel_slot(i);
      if (slot >= 0 && CAR_Z[slot] == z)
        lv_obj_move_foreground(s_cards[i]);
    }
  }
}

static void update_view(void) {
  lv_label_set_text_fmt(s_label, LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, THEMES[s_sel].label);
  lv_label_set_text(s_active, s_sel == theme_idx ? LV_SYMBOL_OK " APPLIED" : "OK to apply");
  lv_obj_set_style_text_color(
      s_active, s_sel == theme_idx ? current_theme.border_accent : current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_active, s_sel == theme_idx ? LV_OPA_COVER : LV_OPA_50, 0);

  page_dots_set(&s_dots, s_sel);
  for (int i = 0; i < THEME_COUNT; i++)
    place_card(i);
  fix_z_order();
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  bool next = (right && !s_right_last) || (down && !s_down_last);
  bool prev = (left && !s_left_last) || (up && !s_up_last);

  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_SETTINGS);
    return;
  }
  if (ok && !s_ok_last) {
    if (s_sel != theme_idx) {
      theme_idx = s_sel;
      ui_theme_load_idx(s_sel);
      ESP_LOGI(TAG, "applied theme %d (%s)", s_sel, THEMES[s_sel].label);
      build_screen();
      return;
    }
  }
  if (next || prev) {
    if (next)
      s_sel = (s_sel + 1) % THEME_COUNT;
    else
      s_sel = (s_sel == 0) ? THEME_COUNT - 1 : s_sel - 1;
    ui_feedback(UI_FB_NAV);
    update_view();
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

static void build_screen(void) {
  lv_obj_t *prev = s_screen;

  if (s_base_dsc == NULL)
    s_base_dsc = assets_get(BASE_FRAME);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "THEME", TITLE_ICON);
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " Browse   " LV_SYMBOL_OK " Apply");

  for (int i = 0; i < THEME_COUNT; i++)
    s_cards[i] = make_card(s_screen, i);

  s_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_label, current_theme.border_accent, 0);
  lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 52);

  s_active = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_active, &lv_font_montserrat_12, 0);
  lv_obj_align(s_active, LV_ALIGN_BOTTOM_MID, 0, -42);

  s_dots = page_dots_create(s_screen, THEME_COUNT, LV_ALIGN_BOTTOM_MID, 0, -26);

  if (s_sel < 0)
    s_sel = 0;
  if (s_sel >= THEME_COUNT)
    s_sel = THEME_COUNT - 1;
  update_view();

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
  if (prev != NULL)
    lv_obj_del(prev);
}

void ui_theme_selector_open(void) {
  s_sel = (theme_idx >= 0 && theme_idx < THEME_COUNT) ? theme_idx : 0;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  build_screen();
}

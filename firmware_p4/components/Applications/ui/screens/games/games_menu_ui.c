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

#include "games_menu_ui.h"

#include "assets_manager.h"
#include "notify_ui.h"
#include "page_dots_ui.h"
#include "st7789.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TARGET_STUB (-1)

#define TITLE_ICON    "/assets/icons/sports_esports.bin"
#define BASE_FRAME    "/assets/frames/base_frame_0.bin"
#define CARD_Y_BIAS   (-18)
#define CONTRAST_DARK 0x0A0220

typedef struct {
  const char *name;
  const char *icon;
  uint32_t color;
  bool dark_glyph;
  int target;
} game_t;

static const game_t GAMES[] = {
    {"Octo Pet", "/assets/icons/game_pet.bin", 0x834EC6, false, SCREEN_GAME_OCTOPET},
    {"Octo Flap", "/assets/icons/game_flap.bin", 0x00E5D0, true, SCREEN_GAME_FLAPPY},
    {"Snake", "/assets/icons/game_snake.bin", 0x00E676, true, SCREEN_GAME_SNAKE},
    {"Breakout", "/assets/icons/game_brk.bin", 0xFFB020, true, SCREEN_GAME_BREAKOUT},
    {"Motion / Level", "/assets/icons/sensors.bin", 0x22D3EE, true, SCREEN_MOTION},
    {"Coming Soon", "/assets/icons/timer.bin", 0x5E12A0, false, TARGET_STUB},
};
#define GAME_COUNT ((int)(sizeof(GAMES) / sizeof(GAMES[0])))

static const int32_t CAR_PX[] = {-94, -50, 0, 50, 94};
static const int32_t CAR_PY[] = {-14, -6, 0, -6, -14};
static const int32_t CAR_SC[] = {117, 161, 234, 161, 117};
static const int32_t GLYPH_SC[] = {170, 235, 341, 235, 170};
static const int32_t CAR_OP[] = {LV_OPA_50, LV_OPA_80, LV_OPA_COVER, LV_OPA_80, LV_OPA_50};
static const int32_t CAR_Z[] = {0, 1, 2, 1, 0};
#define CAR_SLOTS  5
#define CAR_CENTER 2

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_base[GAME_COUNT];
static lv_obj_t *s_glyph[GAME_COUNT];
static lv_obj_t *s_label = NULL;
static page_dots_t s_dots;
static lv_image_dsc_t *s_base_dsc = NULL;
static int s_sel = 0;

static int32_t carousel_slot(int item_idx) {
  int32_t n = GAME_COUNT;
  int32_t d = (item_idx - s_sel + n) % n;
  if (d > n / 2)
    d -= n;
  int32_t slot = CAR_CENTER + d;
  return (slot >= 0 && slot < CAR_SLOTS) ? slot : -1;
}

static void make_card(lv_obj_t *parent, int i) {
  const game_t *g = &GAMES[i];

  lv_obj_t *base = lv_image_create(parent);
  if (s_base_dsc)
    lv_image_set_src(base, s_base_dsc);
  lv_image_set_antialias(base, false);
  lv_obj_align(base, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
  lv_obj_set_style_image_recolor(base, lv_color_hex(g->color), 0);
  lv_obj_set_style_image_recolor_opa(base, LV_OPA_COVER, 0);
  s_base[i] = base;

  lv_obj_t *glyph = lv_image_create(parent);
  lv_image_dsc_t *gd = assets_get(g->icon);
  if (gd)
    lv_image_set_src(glyph, gd);
  lv_image_set_antialias(glyph, false);
  lv_obj_align(glyph, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
  lv_obj_set_style_image_recolor(
      glyph, g->dark_glyph ? lv_color_hex(CONTRAST_DARK) : lv_color_white(), 0);
  lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
  s_glyph[i] = glyph;
}

static void place_card(int i) {
  if (s_base[i] == NULL || s_glyph[i] == NULL)
    return;
  int32_t slot = carousel_slot(i);

  if (slot < 0) {
    lv_obj_add_flag(s_base[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_glyph[i], LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(s_base[i], LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_glyph[i], LV_OBJ_FLAG_HIDDEN);

  int32_t x = CAR_PX[slot];
  int32_t y = CAR_PY[slot] + CARD_Y_BIAS;

  lv_obj_align(s_base[i], LV_ALIGN_CENTER, x, y);
  lv_image_set_scale(s_base[i], CAR_SC[slot]);
  lv_obj_set_style_opa(s_base[i], CAR_OP[slot], 0);

  lv_obj_align(s_glyph[i], LV_ALIGN_CENTER, x, y);
  lv_image_set_scale(s_glyph[i], GLYPH_SC[slot]);
  lv_obj_set_style_opa(s_glyph[i], CAR_OP[slot], 0);
}

static void fix_z_order(void) {
  for (int z = 0; z <= CAR_CENTER; z++) {
    for (int i = 0; i < GAME_COUNT; i++) {
      int32_t slot = carousel_slot(i);
      if (slot >= 0 && CAR_Z[slot] == z) {
        lv_obj_move_foreground(s_base[i]);
        lv_obj_move_foreground(s_glyph[i]);
      }
    }
  }
}

static void update_view(void) {
  lv_label_set_text_fmt(s_label, LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, GAMES[s_sel].name);
  page_dots_set(&s_dots, s_sel);
  for (int i = 0; i < GAME_COUNT; i++)
    place_card(i);
  fix_z_order();
}

static void games_menu_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_OK:
      if (press) {
        if (GAMES[s_sel].target == TARGET_STUB)
          notify(NOTIFY_INFO, "More apps coming soon");
        else
          ui_switch_screen(GAMES[s_sel].target);
      }
      break;
    case INPUT_BTN_RIGHT:
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % GAME_COUNT;
        ui_feedback(UI_FB_NAV);
        update_view();
      }
      break;
    case INPUT_BTN_LEFT:
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel == 0) ? GAME_COUNT - 1 : s_sel - 1;
        ui_feedback(UI_FB_NAV);
        update_view();
      }
      break;
    default:
      break;
  }
}

void ui_games_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;

  if (s_base_dsc == NULL)
    s_base_dsc = assets_get(BASE_FRAME);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "APPS", TITLE_ICON);
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " Browse   " LV_SYMBOL_OK " Play");

  for (int i = 0; i < GAME_COUNT; i++)
    make_card(s_screen, i);

  s_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_label, current_theme.border_accent, 0);
  lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 52);

  s_dots = page_dots_create(s_screen, GAME_COUNT, LV_ALIGN_BOTTOM_MID, 0, -26);

  update_view();

  ui_input_set_screen_handler(games_menu_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

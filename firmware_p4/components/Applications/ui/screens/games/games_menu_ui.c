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

// The APPS carousel: built-in apps (the games) plus every signed .hb app on the SD
// card, side by side. Built-ins show a recolored glyph; SD apps show their embedded
// icon (v3 .hb) inside the frame, or a generic glyph when they ship none. OK plays a
// built-in or launches an SD app (via the shared app registry); the app list is
// rescanned every time the screen opens.

#include "games_menu_ui.h"

#include <string.h>

#include "app_registry.h"
#include "assets_manager.h"
#include "notify_ui.h"
#include "page_dots_ui.h"
#include "st7789.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TARGET_STUB (-1)
#define TARGET_HB   (-2) // OK launches the SD app named in item.file

#define TITLE_ICON        "/assets/icons/sports_esports.bin"
#define BASE_FRAME        "/assets/frames/base_frame_0.bin"
#define APP_FALLBACK_ICON "/assets/icons/description.bin"
#define CARD_Y_BIAS       (-18)
#define CONTRAST_DARK     0x0A0220
#define HB_FRAME_COLOR    0x2E90FF // SD apps get a distinct (blue) frame vs games
#define ICON_TARGET_PX    44       // center display size for a SD app's embedded icon

typedef struct {
  const char *name;    // built-in label (static); SD apps use name_buf instead
  const char *icon;    // built-in glyph asset path (recolored)
  uint32_t color;      // frame recolor
  bool dark_glyph;     // built-in glyph: dark vs white recolor
  int target;          // built-in screen, TARGET_STUB, or TARGET_HB
} game_t;

static const game_t GAMES[] = {
    {"Snake", "/assets/icons/game_snake.bin", 0x00E676, true, SCREEN_GAME_SNAKE},
    {"Breakout", "/assets/icons/game_brk.bin", 0xFFB020, true, SCREEN_GAME_BREAKOUT},
    {"Game Boy", "/assets/icons/game_gb.bin", 0x9BBC0F, true, SCREEN_GAME_GB},
    {"DOOM", "/assets/icons/timer.bin", 0xC81E1E, false, SCREEN_GAME_DOOM},
};
#define GAME_COUNT ((int)(sizeof(GAMES) / sizeof(GAMES[0])))
#define ITEM_MAX   (GAME_COUNT + APP_REG_MAX)

// One carousel card: a built-in game or an SD .hb app.
typedef struct {
  char name[64];                  // display name
  const char *builtin_icon;       // asset path glyph (recolored), or NULL
  const lv_image_dsc_t *hb_icon;  // embedded SD-app icon (native color), or NULL
  uint32_t color;                 // frame recolor
  bool dark_glyph;                // for builtin_icon: dark vs white
  int target;                     // built-in screen, TARGET_STUB, or TARGET_HB
  char file[32];                  // SD-app identity (launch), for TARGET_HB
} item_t;

static const int32_t CAR_PX[] = {-94, -50, 0, 50, 94};
static const int32_t CAR_PY[] = {-14, -6, 0, -6, -14};
static const int32_t CAR_SC[] = {117, 161, 234, 161, 117};
static const int32_t GLYPH_SC[] = {170, 235, 341, 235, 170};
static const int32_t CAR_OP[] = {LV_OPA_50, LV_OPA_80, LV_OPA_COVER, LV_OPA_80, LV_OPA_50};
static const int32_t CAR_Z[] = {0, 1, 2, 1, 0};
#define CAR_SLOTS  5
#define CAR_CENTER 2

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_base[ITEM_MAX];
static lv_obj_t *s_glyph[ITEM_MAX];
static lv_obj_t *s_label = NULL;
static page_dots_t s_dots;
static lv_image_dsc_t *s_base_dsc = NULL;
static int s_sel = 0;

static item_t s_items[ITEM_MAX];
static int s_item_count = 0;
static app_reg_entry_t s_reg[APP_REG_MAX];
static int s_reg_count = 0;

static void build_items(void) {
  app_registry_free(s_reg, s_reg_count); // release the previous scan's icon pixels
  s_reg_count = 0;
  s_item_count = 0;

  for (int i = 0; i < GAME_COUNT && s_item_count < ITEM_MAX; i++) {
    item_t *it = &s_items[s_item_count++];
    memset(it, 0, sizeof(*it));
    strlcpy(it->name, GAMES[i].name, sizeof(it->name));
    it->builtin_icon = GAMES[i].icon;
    it->hb_icon = NULL;
    it->color = GAMES[i].color;
    it->dark_glyph = GAMES[i].dark_glyph;
    it->target = GAMES[i].target;
  }

  s_reg_count = app_registry_scan(s_reg, APP_REG_MAX);
  for (int i = 0; i < s_reg_count && s_item_count < ITEM_MAX; i++) {
    const app_reg_entry_t *r = &s_reg[i];
    item_t *it = &s_items[s_item_count++];
    memset(it, 0, sizeof(*it));
    strlcpy(it->name, r->title[0] ? r->title : r->file, sizeof(it->name));
    strlcpy(it->file, r->file, sizeof(it->file));
    it->color = HB_FRAME_COLOR;
    it->target = TARGET_HB;
    if (r->has_icon) {
      it->hb_icon = &r->icon; // native-color embedded icon
    } else {
      it->builtin_icon = APP_FALLBACK_ICON; // generic white glyph
      it->dark_glyph = false;
    }
  }
}

static int32_t carousel_slot(int item_idx) {
  int32_t n = s_item_count;
  int32_t d = (item_idx - s_sel + n) % n;
  if (d > n / 2)
    d -= n;
  int32_t slot = CAR_CENTER + d;
  return (slot >= 0 && slot < CAR_SLOTS) ? slot : -1;
}

static void make_card(lv_obj_t *parent, int i) {
  const item_t *it = &s_items[i];

  lv_obj_t *base = lv_image_create(parent);
  if (s_base_dsc)
    lv_image_set_src(base, s_base_dsc);
  lv_image_set_antialias(base, false);
  lv_obj_align(base, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
  lv_obj_set_style_image_recolor(base, lv_color_hex(it->color), 0);
  lv_obj_set_style_image_recolor_opa(base, LV_OPA_COVER, 0);
  s_base[i] = base;

  lv_obj_t *glyph = lv_image_create(parent);
  if (it->hb_icon != NULL) {
    lv_image_set_src(glyph, it->hb_icon);
    lv_image_set_antialias(glyph, true); // full-color icon, scaled -> smoother
  } else {
    lv_image_dsc_t *gd = assets_get(it->builtin_icon);
    if (gd)
      lv_image_set_src(glyph, gd);
    lv_image_set_antialias(glyph, false);
    lv_obj_set_style_image_recolor(
        glyph, it->dark_glyph ? lv_color_hex(CONTRAST_DARK) : lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
  }
  lv_obj_align(glyph, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
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
  // Built-in glyphs are pre-sized, so GLYPH_SC fits them; a SD app's embedded icon
  // can be any size (16..64 px), so normalize it to ~ICON_TARGET_PX at center and
  // scale per slot from there, keeping the carousel depth effect.
  int32_t gscale = GLYPH_SC[slot];
  if (s_items[i].hb_icon != NULL && s_items[i].hb_icon->header.w > 0) {
    int32_t center = (int32_t)ICON_TARGET_PX * 256 / (int32_t)s_items[i].hb_icon->header.w;
    gscale = center * GLYPH_SC[slot] / GLYPH_SC[CAR_CENTER];
  }
  lv_image_set_scale(s_glyph[i], gscale);
  lv_obj_set_style_opa(s_glyph[i], CAR_OP[slot], 0);
}

static void fix_z_order(void) {
  for (int z = 0; z <= CAR_CENTER; z++) {
    for (int i = 0; i < s_item_count; i++) {
      int32_t slot = carousel_slot(i);
      if (slot >= 0 && CAR_Z[slot] == z) {
        lv_obj_move_foreground(s_base[i]);
        lv_obj_move_foreground(s_glyph[i]);
      }
    }
  }
}

static void update_view(void) {
  lv_label_set_text_fmt(s_label, LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, s_items[s_sel].name);
  page_dots_set(&s_dots, s_sel);
  for (int i = 0; i < s_item_count; i++)
    place_card(i);
  fix_z_order();
}

static void activate_selected(void) {
  const item_t *it = &s_items[s_sel];
  if (it->target == TARGET_HB)
    app_registry_launch(it->file);
  else if (it->target == TARGET_STUB)
    notify(NOTIFY_INFO, "More apps coming soon");
  else
    ui_switch_screen(it->target);
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
      if (press)
        activate_selected();
      break;
    case INPUT_BTN_RIGHT:
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % s_item_count;
        ui_feedback(UI_FB_NAV);
        update_view();
      }
      break;
    case INPUT_BTN_LEFT:
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel == 0) ? s_item_count - 1 : s_sel - 1;
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

  build_items(); // built-ins + a fresh scan of /sdcard/apps

  if (s_base_dsc == NULL)
    s_base_dsc = assets_get(BASE_FRAME);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "APPS", TITLE_ICON);
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " Browse   " LV_SYMBOL_OK " Open");

  for (int i = 0; i < s_item_count; i++)
    make_card(s_screen, i);

  s_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_label, current_theme.border_accent, 0);
  lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 52);

  s_dots = page_dots_create(s_screen, s_item_count, LV_ALIGN_BOTTOM_MID, 0, -26);

  update_view();

  ui_input_set_screen_handler(games_menu_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

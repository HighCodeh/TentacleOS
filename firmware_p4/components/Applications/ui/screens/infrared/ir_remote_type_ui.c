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

#include "ir_remote_type_ui.h"

#include "assets_manager.h"
#include "ir_controller_ui.h"
#include "page_dots_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TITLE_ICON    "/assets/icons/settings_remote.bin"
#define BASE_FRAME    "/assets/frames/base_frame_0.bin"
#define CARD_Y_BIAS   (-18)
#define CONTRAST_DARK 0x0A0220
#define LABEL_Y       52
#define DOTS_Y        (-26)

typedef struct {
  const char *name;
  const char *icon;
  uint32_t color;
  bool dark_glyph;
  ir_device_t dev;
} device_t;

static const device_t DEVICES[] = {
    {"TV", "/assets/icons/tv.bin", 0x834EC6, false, IR_DEV_TV},
    {"Sound System", "/assets/icons/speaker.bin", 0xFFB020, true, IR_DEV_SOUND},
    {"Air Conditioner", "/assets/icons/ac_unit.bin", 0x00E5D0, true, IR_DEV_AC},
};
#define DEVICE_COUNT ((int)(sizeof(DEVICES) / sizeof(DEVICES[0])))

static const int32_t CAR_PX[] = {-94, -50, 0, 50, 94};
static const int32_t CAR_PY[] = {-14, -6, 0, -6, -14};
static const int32_t CAR_SC[] = {117, 161, 234, 161, 117};
static const int32_t GLYPH_SC[] = {170, 235, 341, 235, 170};
static const int32_t CAR_OP[] = {LV_OPA_50, LV_OPA_80, LV_OPA_COVER, LV_OPA_80, LV_OPA_50};
static const int32_t CAR_Z[] = {0, 1, 2, 1, 0};
#define CAR_SLOTS  5
#define CAR_CENTER 2

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_base[DEVICE_COUNT];
static lv_obj_t *s_glyph[DEVICE_COUNT];
static lv_obj_t *s_label = NULL;
static page_dots_t s_dots;
static lv_image_dsc_t *s_base_dsc = NULL;
static int s_sel = 0;

static int32_t carousel_slot(int item_idx) {
  int32_t n = DEVICE_COUNT;
  int32_t d = (item_idx - s_sel + n) % n;
  if (d > n / 2)
    d -= n;
  int32_t slot = CAR_CENTER + d;
  return (slot >= 0 && slot < CAR_SLOTS) ? slot : -1;
}

static void make_card(lv_obj_t *parent, int i) {
  const device_t *dev = &DEVICES[i];

  lv_obj_t *base = lv_image_create(parent);
  if (s_base_dsc)
    lv_image_set_src(base, s_base_dsc);
  lv_image_set_antialias(base, false);
  lv_obj_align(base, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
  lv_obj_set_style_image_recolor(base, lv_color_hex(dev->color), 0);
  lv_obj_set_style_image_recolor_opa(base, LV_OPA_COVER, 0);
  s_base[i] = base;

  lv_obj_t *glyph = lv_image_create(parent);
  lv_image_dsc_t *gd = assets_get(dev->icon);
  if (gd)
    lv_image_set_src(glyph, gd);
  lv_image_set_antialias(glyph, false);
  lv_obj_align(glyph, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
  lv_obj_set_style_image_recolor(
      glyph, dev->dark_glyph ? lv_color_hex(CONTRAST_DARK) : lv_color_white(), 0);
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
    for (int i = 0; i < DEVICE_COUNT; i++) {
      int32_t slot = carousel_slot(i);
      if (slot >= 0 && CAR_Z[slot] == z) {
        lv_obj_move_foreground(s_base[i]);
        lv_obj_move_foreground(s_glyph[i]);
      }
    }
  }
}

static void update_view(void) {
  lv_label_set_text_fmt(s_label, LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, DEVICES[s_sel].name);
  page_dots_set(&s_dots, s_sel);
  for (int i = 0; i < DEVICE_COUNT; i++)
    place_card(i);
  fix_z_order();
}

static void ir_remote_type_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_IR_MENU);
      break;
    case INPUT_BTN_OK:
      if (press) {
        ui_feedback(UI_FB_SELECT);
        ui_ir_controller_set_device(DEVICES[s_sel].dev);
        ui_switch_screen(SCREEN_IR_CONTROLLER);
      }
      break;
    case INPUT_BTN_DOWN:
    case INPUT_BTN_RIGHT:
      if (nav) {
        s_sel = (s_sel + 1) % DEVICE_COUNT;
        ui_feedback(UI_FB_NAV);
        update_view();
      }
      break;
    case INPUT_BTN_UP:
    case INPUT_BTN_LEFT:
      if (nav) {
        s_sel = (s_sel == 0) ? DEVICE_COUNT - 1 : s_sel - 1;
        ui_feedback(UI_FB_NAV);
        update_view();
      }
      break;
    default:
      break;
  }
}

void ui_ir_remote_type_open(void) {
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
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "REMOTE", TITLE_ICON);
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " Browse   " LV_SYMBOL_OK " Select");

  for (int i = 0; i < DEVICE_COUNT; i++)
    make_card(s_screen, i);

  s_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_label, current_theme.border_accent, 0);
  lv_obj_align(s_label, LV_ALIGN_CENTER, 0, LABEL_Y);

  s_dots = page_dots_create(s_screen, DEVICE_COUNT, LV_ALIGN_BOTTOM_MID, 0, DOTS_Y);

  update_view();

  ui_input_set_screen_handler(ir_remote_type_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

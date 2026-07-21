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

#include "ir_controller_ui.h"

#include "esp_log.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_CTRL_UI";

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            180
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2
#define NAV_TIMER_INTERVAL_MS  50
#define MAX_BTNS               16
#define BTN_Y_OFFSET           (-8)

typedef struct {
  const char *text;
  int dx, dy, w, h;
} rc_btn_t;

typedef struct {
  const char *title;
  const rc_btn_t *btns;
  int count;
  int start;
} rc_layout_t;

static const rc_btn_t TV_BTNS[] = {
    {LV_SYMBOL_POWER, -72, 58, 44, 34},
    {LV_SYMBOL_MUTE, 72, 58, 44, 34},
    {LV_SYMBOL_UP, 0, 98, 40, 28},
    {LV_SYMBOL_LEFT, -54, 138, 40, 34},
    {"OK", 0, 132, 56, 56},
    {LV_SYMBOL_RIGHT, 54, 138, 40, 34},
    {LV_SYMBOL_DOWN, 0, 192, 40, 28},
    {"VOL +", -78, 234, 52, 30},
    {"VOL -", -78, 270, 52, 30},
    {LV_SYMBOL_LIST, 0, 234, 46, 30},
    {LV_SYMBOL_HOME, 0, 270, 46, 30},
    {"CH +", 78, 234, 52, 30},
    {"CH -", 78, 270, 52, 30},
};

static const rc_btn_t SOUND_BTNS[] = {
    {LV_SYMBOL_POWER, -70, 60, 56, 34},
    {"SRC", 70, 60, 56, 34},
    {"VOL -", -70, 116, 56, 34},
    {"VOL +", 70, 116, 56, 34},
    {LV_SYMBOL_PREV, -74, 176, 50, 40},
    {LV_SYMBOL_PLAY, 0, 174, 56, 46},
    {LV_SYMBOL_NEXT, 74, 176, 50, 40},
    {LV_SYMBOL_MUTE, -70, 240, 56, 34},
    {"MODE", 70, 240, 56, 34},
};

static const rc_btn_t AC_BTNS[] = {
    {LV_SYMBOL_POWER, -70, 60, 56, 34},
    {"MODE", 70, 60, 56, 34},
    {"TEMP +", 0, 116, 78, 36},
    {"TEMP -", 0, 166, 78, 36},
    {"FAN", -70, 222, 56, 34},
    {"SWING", 70, 222, 56, 34},
    {"TIMER", -70, 272, 56, 34},
    {"ECO", 70, 272, 56, 34},
};

static const rc_layout_t LAYOUTS[] = {
    [IR_DEV_TV] = {"TV Remote", TV_BTNS, (int)(sizeof(TV_BTNS) / sizeof(TV_BTNS[0])), 4},
    [IR_DEV_SOUND] = {"Sound System",
                      SOUND_BTNS,
                      (int)(sizeof(SOUND_BTNS) / sizeof(SOUND_BTNS[0])),
                      5},
    [IR_DEV_AC] = {"Air Cond.", AC_BTNS, (int)(sizeof(AC_BTNS) / sizeof(AC_BTNS[0])), 2},
};
#define LAYOUT_COUNT ((int)(sizeof(LAYOUTS) / sizeof(LAYOUTS[0])))

static ir_device_t s_device = IR_DEV_TV;
static const rc_layout_t *s_lay = &LAYOUTS[IR_DEV_TV];

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_btn_objs[MAX_BTNS];
static lv_timer_t *s_nav_timer = NULL;
static int s_focus = 0;

static bool s_btn_up_last, s_btn_down_last, s_btn_left_last;
static bool s_btn_right_last, s_btn_ok_last, s_btn_back_last;

static void nav_timer_cb(lv_timer_t *timer);

void ui_ir_controller_set_device(ir_device_t dev) {
  if ((int)dev >= 0 && (int)dev < LAYOUT_COUNT)
    s_device = dev;
}

static void apply_focus_style(lv_obj_t *btn, bool focused) {
  lv_obj_set_style_border_color(
      btn, focused ? current_theme.border_accent : current_theme.border_interface, 0);
  lv_obj_set_style_border_width(btn, focused ? 3 : 2, 0);
}

static void set_focus(int idx) {
  if (idx < 0 || idx >= s_lay->count || idx == s_focus)
    return;
  apply_focus_style(s_btn_objs[s_focus], false);
  s_focus = idx;
  apply_focus_style(s_btn_objs[s_focus], true);
}

static int neighbor(int dir) {
  const rc_btn_t *cur = &s_lay->btns[s_focus];
  int ccx = cur->dx, ccy = cur->dy;
  int best = -1;
  long best_cost = 0;
  for (int i = 0; i < s_lay->count; i++) {
    if (i == s_focus)
      continue;
    const rc_btn_t *b = &s_lay->btns[i];
    int ddx = b->dx - ccx;
    int ddy = b->dy - ccy;
    int along, perp;
    bool ok;
    switch (dir) {
      case 0:
        ok = ddy < -4;
        along = -ddy;
        perp = ddx < 0 ? -ddx : ddx;
        break;
      case 1:
        ok = ddy > 4;
        along = ddy;
        perp = ddx < 0 ? -ddx : ddx;
        break;
      case 2:
        ok = ddx < -4;
        along = -ddx;
        perp = ddy < 0 ? -ddy : ddy;
        break;
      default:
        ok = ddx > 4;
        along = ddx;
        perp = ddy < 0 ? -ddy : ddy;
        break;
    }
    if (!ok)
      continue;
    long cost = (long)along + 2L * perp;
    if (best < 0 || cost < best_cost) {
      best = i;
      best_cost = cost;
    }
  }
  return best;
}

static lv_obj_t *make_button(const rc_btn_t *def) {
  lv_obj_t *btn = lv_obj_create(s_screen);
  lv_obj_set_size(btn, def->w, def->h);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, def->dx, def->dy + BTN_Y_OFFSET);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(btn, (def->w == def->h) ? LV_RADIUS_CIRCLE : 10, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, current_theme.bg_item_bot, 0);
  lv_obj_set_style_bg_grad_color(btn, current_theme.bg_item_top, 0);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, current_theme.border_interface, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, def->text);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(lbl);

  return btn;
}

void ui_ir_controller_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_lay = &LAYOUTS[s_device];

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, s_lay->title, "/assets/icons/remote_menu_icon.bin");

  for (int i = 0; i < s_lay->count && i < MAX_BTNS; i++)
    s_btn_objs[i] = make_button(&s_lay->btns[i]);

  s_focus = (s_lay->start >= 0 && s_lay->start < s_lay->count) ? s_lay->start : 0;
  apply_focus_style(s_btn_objs[s_focus], true);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_chrome_footer(s_screen, "OK = Send   BACK = Back");

  ui_screen_load(s_screen);
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
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

  if (back && !s_btn_back_last)
    ui_switch_screen(SCREEN_IR_REMOTE_TYPE);

  if (up && !s_btn_up_last) {
    int n = neighbor(0);
    if (n >= 0)
      set_focus(n);
  }
  if (down && !s_btn_down_last) {
    int n = neighbor(1);
    if (n >= 0)
      set_focus(n);
  }
  if (left && !s_btn_left_last) {
    int n = neighbor(2);
    if (n >= 0)
      set_focus(n);
  }
  if (right && !s_btn_right_last) {
    int n = neighbor(3);
    if (n >= 0)
      set_focus(n);
  }

  if (ok && !s_btn_ok_last)
    ESP_LOGI(TAG, "press [%s]: %s", s_lay->title, s_lay->btns[s_focus].text);

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

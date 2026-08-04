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

#include <stdio.h>

#include "esp_log.h"

#include "buttons_gpio.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_CTRL_UI";

#define COL_DIM 0x8A8594

#define NAV_TIMER_INTERVAL_MS 50
#define MAX_BTNS              16

#define KEYPAD_W 232
#define KEYPAD_H 250
#define KEYPAD_Y 44

#define FLASH_MS 150

#define AC_FIELD_COUNT 4
#define AC_F_POWER     0
#define AC_F_MODE      1
#define AC_F_TEMP      2
#define AC_F_FAN       3

#define AC_TEMP_MIN     16
#define AC_TEMP_MAX     30
#define AC_TEMP_DEFAULT 22
#define AC_FAN_DEFAULT  1
#define AC_ON_COLOR     0x00E676

#define AC_ROW_W      210
#define AC_ROW_H      48
#define AC_ROW_GAP    10
#define AC_ROW_RADIUS 10
#define AC_ROW_PAD    16
#define AC_ROW_BORDER 2
#define AC_ROW_GLOW   14
#define AC_ROW_SPREAD (-3)

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
    {LV_SYMBOL_POWER, -64, 8, 52, 32},
    {LV_SYMBOL_MUTE, 64, 8, 52, 32},
    {LV_SYMBOL_UP, 0, 50, 44, 30},
    {LV_SYMBOL_LEFT, -54, 96, 42, 34},
    {"OK", 0, 90, 54, 52},
    {LV_SYMBOL_RIGHT, 54, 96, 42, 34},
    {LV_SYMBOL_DOWN, 0, 148, 44, 30},
    {"VOL +", -76, 186, 54, 28},
    {"VOL -", -76, 218, 54, 28},
    {LV_SYMBOL_LIST, 0, 186, 48, 28},
    {LV_SYMBOL_HOME, 0, 218, 48, 28},
    {"CH +", 76, 186, 54, 28},
    {"CH -", 76, 218, 54, 28},
};

static const rc_btn_t SOUND_BTNS[] = {
    {LV_SYMBOL_POWER, -58, 16, 58, 34},
    {"SRC", 58, 16, 58, 34},
    {"VOL -", -58, 68, 58, 34},
    {"VOL +", 58, 68, 58, 34},
    {LV_SYMBOL_PREV, -70, 128, 50, 42},
    {LV_SYMBOL_PLAY, 0, 124, 58, 50},
    {LV_SYMBOL_NEXT, 70, 128, 50, 42},
    {LV_SYMBOL_MUTE, -58, 194, 58, 34},
    {"MODE", 58, 194, 58, 34},
};

static const rc_btn_t AC_BTNS[] = {
    {LV_SYMBOL_POWER, -58, 14, 58, 34},
    {"MODE", 58, 14, 58, 34},
    {"TEMP +", 0, 64, 80, 38},
    {"TEMP -", 0, 110, 80, 38},
    {"FAN", -58, 162, 58, 34},
    {"SWING", 58, 162, 58, 34},
    {"TIMER", -58, 206, 58, 34},
    {"ECO", 58, 206, 58, 34},
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

static const char *const AC_LABELS[AC_FIELD_COUNT] = {"Power", "Mode", "Temp", "Fan"};
static const char *const AC_MODES[] = {"Cool", "Heat", "Fan", "Auto"};
#define AC_MODE_COUNT ((int)(sizeof(AC_MODES) / sizeof(AC_MODES[0])))
static const char *const AC_FANS[] = {"Low", "Med", "High", "Auto"};
#define AC_FAN_COUNT ((int)(sizeof(AC_FANS) / sizeof(AC_FANS[0])))

static ir_device_t s_device = IR_DEV_TV;
static const rc_layout_t *s_lay = &LAYOUTS[IR_DEV_TV];

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_keypad = NULL;
static lv_obj_t *s_btn_objs[MAX_BTNS];
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_flash_timer = NULL;
static int s_focus = 0;

static bool s_is_ac = false;
static lv_obj_t *s_ac_rows[AC_FIELD_COUNT];
static lv_obj_t *s_ac_vals[AC_FIELD_COUNT];
static int s_ac_sel = 0;
static bool s_ac_power = true;
static int s_ac_mode = 0;
static int s_ac_temp = AC_TEMP_DEFAULT;
static int s_ac_fan = AC_FAN_DEFAULT;

static bool s_btn_up_last, s_btn_down_last, s_btn_left_last;
static bool s_btn_right_last, s_btn_ok_last, s_btn_back_last;

static void nav_timer_cb(lv_timer_t *timer);

void ui_ir_controller_set_device(ir_device_t dev) {
  if ((int)dev >= 0 && (int)dev < LAYOUT_COUNT)
    s_device = dev;
}

static void apply_focus_style(lv_obj_t *btn, bool focused) {
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  lv_obj_set_style_bg_color(btn, current_theme.bg_secondary, 0);
  if (focused) {
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_shadow_color(btn, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(btn, 14, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_shadow_spread(btn, -3, 0);
    if (lbl)
      lv_obj_set_style_text_color(lbl, current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_color(btn, current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    if (lbl)
      lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
  }
}

static void set_focus(int idx) {
  if (idx < 0 || idx >= s_lay->count || idx == s_focus)
    return;
  apply_focus_style(s_btn_objs[s_focus], false);
  s_focus = idx;
  apply_focus_style(s_btn_objs[s_focus], true);
  ui_feedback(UI_FB_NAV);
}

static void ac_apply_row_style(lv_obj_t *row, bool focused) {
  lv_obj_t *name = lv_obj_get_child(row, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
  if (focused) {
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(row, AC_ROW_BORDER, 0);
    lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, AC_ROW_GLOW, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_50, 0);
    lv_obj_set_style_shadow_spread(row, AC_ROW_SPREAD, 0);
    if (name)
      lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  } else {
    lv_obj_set_style_bg_opa(row, LV_OPA_80, 0);
    lv_obj_set_style_border_color(row, current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(row, AC_ROW_BORDER, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
    if (name)
      lv_obj_set_style_text_color(name, lv_color_hex(COL_DIM), 0);
  }
}

static void ac_update_values(void) {
  char temp_buf[8];
  snprintf(temp_buf, sizeof(temp_buf), "%d C", s_ac_temp);
  lv_label_set_text(s_ac_vals[AC_F_POWER], s_ac_power ? "On" : "Off");
  lv_label_set_text(s_ac_vals[AC_F_MODE], AC_MODES[s_ac_mode]);
  lv_label_set_text(s_ac_vals[AC_F_TEMP], temp_buf);
  lv_label_set_text(s_ac_vals[AC_F_FAN], AC_FANS[s_ac_fan]);

  lv_color_t active = current_theme.border_accent;
  lv_color_t idle = lv_color_hex(COL_DIM);
  lv_color_t body = s_ac_power ? active : idle;
  lv_obj_set_style_text_color(
      s_ac_vals[AC_F_POWER], s_ac_power ? lv_color_hex(AC_ON_COLOR) : idle, 0);
  lv_obj_set_style_text_color(s_ac_vals[AC_F_MODE], body, 0);
  lv_obj_set_style_text_color(s_ac_vals[AC_F_TEMP], body, 0);
  lv_obj_set_style_text_color(s_ac_vals[AC_F_FAN], body, 0);
}

static void flash_restore_cb(lv_timer_t *t) {
  (void)t;
  s_flash_timer = NULL;
  if (lv_screen_active() != s_screen)
    return;
  if (s_is_ac) {
    ac_apply_row_style(s_ac_rows[s_ac_sel], true);
    ac_update_values();
    return;
  }
  apply_focus_style(s_btn_objs[s_focus], true);
}

static void flash_focus(void) {
  lv_obj_t *btn = s_btn_objs[s_focus];
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  lv_obj_set_style_bg_color(btn, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  if (lbl)
    lv_obj_set_style_text_color(lbl, current_theme.screen_base, 0);
  if (s_flash_timer != NULL)
    lv_timer_delete(s_flash_timer);
  s_flash_timer = lv_timer_create(flash_restore_cb, FLASH_MS, NULL);
  lv_timer_set_repeat_count(s_flash_timer, 1);
}

static lv_obj_t *ac_make_row(const char *label, lv_obj_t **out_val) {
  lv_obj_t *row = lv_obj_create(s_keypad);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, AC_ROW_W, AC_ROW_H);
  lv_obj_set_style_radius(row, AC_ROW_RADIUS, 0);
  lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_pad_top(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 0, 0);
  lv_obj_set_style_pad_left(row, AC_ROW_PAD, 0);
  lv_obj_set_style_pad_right(row, AC_ROW_PAD, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, label);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);

  lv_obj_t *val = lv_label_create(row);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(val, current_theme.border_accent, 0);

  if (out_val != NULL)
    *out_val = val;
  return row;
}

static void build_ac_panel(void) {
  lv_obj_set_flex_flow(s_keypad, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_keypad, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_keypad, AC_ROW_GAP, 0);

  for (int i = 0; i < AC_FIELD_COUNT; i++)
    s_ac_rows[i] = ac_make_row(AC_LABELS[i], &s_ac_vals[i]);

  s_ac_sel = 0;
  ac_update_values();
  for (int i = 0; i < AC_FIELD_COUNT; i++)
    ac_apply_row_style(s_ac_rows[i], i == s_ac_sel);
}

static void ac_flash(void) {
  lv_obj_t *row = s_ac_rows[s_ac_sel];
  lv_obj_t *name = lv_obj_get_child(row, 0);
  lv_obj_t *val = lv_obj_get_child(row, 1);
  lv_obj_set_style_bg_color(row, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  if (name)
    lv_obj_set_style_text_color(name, current_theme.screen_base, 0);
  if (val)
    lv_obj_set_style_text_color(val, current_theme.screen_base, 0);
  if (s_flash_timer != NULL)
    lv_timer_delete(s_flash_timer);
  s_flash_timer = lv_timer_create(flash_restore_cb, FLASH_MS, NULL);
  lv_timer_set_repeat_count(s_flash_timer, 1);
}

static void ac_set_sel(int idx) {
  if (idx < 0 || idx >= AC_FIELD_COUNT || idx == s_ac_sel)
    return;
  ac_apply_row_style(s_ac_rows[s_ac_sel], false);
  s_ac_sel = idx;
  ac_apply_row_style(s_ac_rows[s_ac_sel], true);
  ui_feedback(UI_FB_NAV);
}

static void ac_send(void) {
  ui_feedback(UI_FB_EMULATE);
  ac_flash();
  char buf[48];
  snprintf(buf,
           sizeof(buf),
           "AC  %s  %s  %d C  %s",
           s_ac_power ? "On" : "Off",
           AC_MODES[s_ac_mode],
           s_ac_temp,
           AC_FANS[s_ac_fan]);
  notify(NOTIFY_INFO, buf);
  ESP_LOGI(TAG,
           "AC send: power=%d mode=%s temp=%d fan=%s",
           s_ac_power,
           AC_MODES[s_ac_mode],
           s_ac_temp,
           AC_FANS[s_ac_fan]);
}

static void ac_change(int dir) {
  switch (s_ac_sel) {
    case AC_F_POWER:
      s_ac_power = !s_ac_power;
      break;
    case AC_F_MODE:
      s_ac_mode = (s_ac_mode + dir + AC_MODE_COUNT) % AC_MODE_COUNT;
      break;
    case AC_F_TEMP:
      s_ac_temp += dir;
      if (s_ac_temp < AC_TEMP_MIN)
        s_ac_temp = AC_TEMP_MIN;
      if (s_ac_temp > AC_TEMP_MAX)
        s_ac_temp = AC_TEMP_MAX;
      break;
    case AC_F_FAN:
      s_ac_fan = (s_ac_fan + dir + AC_FAN_COUNT) % AC_FAN_COUNT;
      break;
    default:
      break;
  }
  ac_update_values();
  ac_send();
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
  lv_obj_t *btn = lv_obj_create(s_keypad);
  lv_obj_set_size(btn, def->w, def->h);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, def->dx, def->dy);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(btn, (def->w == def->h) ? LV_RADIUS_CIRCLE : 10, 0);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, def->text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl);

  apply_focus_style(btn, false);
  return btn;
}

void ui_ir_controller_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  if (s_flash_timer != NULL) {
    lv_timer_delete(s_flash_timer);
    s_flash_timer = NULL;
  }
  s_btn_up_last = s_btn_down_last = s_btn_left_last = false;
  s_btn_right_last = s_btn_ok_last = s_btn_back_last = false;

  s_lay = &LAYOUTS[s_device];

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, s_lay->title, "/assets/icons/settings_remote.bin");

  s_keypad = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_keypad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_keypad, KEYPAD_W, KEYPAD_H);
  lv_obj_align(s_keypad, LV_ALIGN_TOP_MID, 0, KEYPAD_Y);
  lv_obj_set_style_bg_opa(s_keypad, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_keypad, 0, 0);
  lv_obj_set_style_radius(s_keypad, 0, 0);
  lv_obj_set_style_pad_all(s_keypad, 0, 0);

  s_is_ac = (s_device == IR_DEV_AC);

  if (s_is_ac) {
    s_ac_power = true;
    s_ac_mode = 0;
    s_ac_temp = AC_TEMP_DEFAULT;
    s_ac_fan = AC_FAN_DEFAULT;
    build_ac_panel();
  } else {
    for (int i = 0; i < s_lay->count && i < MAX_BTNS; i++)
      s_btn_objs[i] = make_button(&s_lay->btns[i]);

    s_focus = (s_lay->start >= 0 && s_lay->start < s_lay->count) ? s_lay->start : 0;
    apply_focus_style(s_btn_objs[s_focus], true);
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_chrome_footer(s_screen, s_is_ac ? "U/D pick   L/R set   OK send" : "OK Send   BACK Back");

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

  if (back && !s_btn_back_last) {
    ui_switch_screen(SCREEN_IR_REMOTE_TYPE);
    return;
  }

  if (s_is_ac) {
    if (up && !s_btn_up_last)
      ac_set_sel((s_ac_sel + AC_FIELD_COUNT - 1) % AC_FIELD_COUNT);
    if (down && !s_btn_down_last)
      ac_set_sel((s_ac_sel + 1) % AC_FIELD_COUNT);
    if (left && !s_btn_left_last)
      ac_change(-1);
    if (right && !s_btn_right_last)
      ac_change(1);
    if (ok && !s_btn_ok_last)
      ac_change(1);

    s_btn_up_last = up;
    s_btn_down_last = down;
    s_btn_left_last = left;
    s_btn_right_last = right;
    s_btn_ok_last = ok;
    s_btn_back_last = back;
    return;
  }

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

  if (ok && !s_btn_ok_last) {
    const char *key = s_lay->btns[s_focus].text;
    ESP_LOGI(TAG, "press [%s]: %s", s_lay->title, key);
    ui_feedback(UI_FB_EMULATE);
    flash_focus();
    char buf[48];
    snprintf(buf, sizeof(buf), "%s sent", key);
    notify(NOTIFY_INFO, buf);
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

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

#include "ble_mouse_ui.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "BLE_MOUSE_UI";

#define OUTER_BORDER          4
#define TOP_BORDER_H          46
#define TITLE_BAR_W           180
#define TITLE_BAR_H           30
#define NAV_TIMER_INTERVAL_MS 50
#define PAIRING_DURATION_US   1800000

static lv_obj_t *make_screen_with_title(const char *title) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  lv_obj_t *top_area = lv_obj_create(screen);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, 3, 0);
  lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_BAR_W, TITLE_BAR_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, 12, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, 2, 0);
  lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, title);
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(title_lbl);

  return screen;
}

#define RIPPLE_COUNT    3
#define RIPPLE_MIN      30
#define RIPPLE_MAX      132
#define RIPPLE_MS       1800
#define RIPPLE_OFFSET_Y 0

static lv_obj_t *s_pair_screen = NULL;
static lv_timer_t *s_pair_timer = NULL;
static int64_t s_pair_start = 0;
static bool s_pair_back_last = false;

static void ripple_cb(void *var, int32_t v) {
  lv_obj_t *ring = (lv_obj_t *)var;
  int32_t sz = RIPPLE_MIN + (RIPPLE_MAX - RIPPLE_MIN) * v / 255;
  lv_obj_set_size(ring, sz, sz);
  lv_obj_align(ring, LV_ALIGN_CENTER, 0, RIPPLE_OFFSET_Y);
  lv_obj_set_style_opa(ring, (lv_opa_t)(255 - v), 0);
}

static void pair_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_pair_screen) {
    lv_timer_delete(t);
    s_pair_timer = NULL;
    return;
  }
  if (ui_input_is_locked()) {
    s_pair_back_last = back_button_is_down();
    return;
  }

  bool back = back_button_is_down();
  if (back && !s_pair_back_last) {
    ui_switch_screen(SCREEN_BLE_MENU);
    return;
  }
  s_pair_back_last = back;

  if (esp_timer_get_time() - s_pair_start >= PAIRING_DURATION_US)
    ui_switch_screen(SCREEN_BLE_MOUSE);
}

void ui_ble_mouse_pairing_open(void) {
  if (s_pair_screen != NULL) {
    lv_obj_del(s_pair_screen);
    s_pair_screen = NULL;
  }

  s_pair_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_pair_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_pair_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_pair_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_pair_screen, 0, 0);
  lv_obj_set_style_pad_all(s_pair_screen, 0, 0);

  ui_chrome_header(s_pair_screen, "MouseAir", "/assets/icons/mouse_icon.bin");

  for (int i = 0; i < RIPPLE_COUNT; i++) {
    lv_obj_t *ring = lv_obj_create(s_pair_screen);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, current_theme.border_accent, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(ring, RIPPLE_MIN, RIPPLE_MIN);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, RIPPLE_OFFSET_Y);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, ripple_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, RIPPLE_MS);
    lv_anim_set_delay(&a, i * (RIPPLE_MS / RIPPLE_COUNT));
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }

  lv_obj_t *node = lv_obj_create(s_pair_screen);
  lv_obj_remove_flag(node, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(node, 38, 38);
  lv_obj_set_style_radius(node, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(node, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(node, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(node, 0, 0);
  lv_obj_align(node, LV_ALIGN_CENTER, 0, RIPPLE_OFFSET_Y);
  lv_obj_t *bt = lv_label_create(node);
  lv_label_set_text(bt, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(bt, current_theme.text_main, 0);
  lv_obj_center(bt);

  lv_obj_t *cap = lv_label_create(s_pair_screen);
  lv_label_set_text(cap, "Pairing...");
  lv_obj_set_style_text_color(cap, current_theme.text_main, 0);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_align(cap, LV_ALIGN_CENTER, 0, RIPPLE_OFFSET_Y + 92);

  ui_chrome_footer(s_pair_screen, "BACK  Cancel");

  s_pair_start = esp_timer_get_time();
  s_pair_back_last = false;
  if (s_pair_timer == NULL)
    s_pair_timer = lv_timer_create(pair_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load(s_pair_screen);
}

typedef struct {
  const char *text;
  int dx, dy, w, h;
  int up, down, left, right;
} mouse_btn_t;

enum {
  M_LCLICK,
  M_RCLICK,
  M_SCRL_UP,
  M_SCRL_DN,
  M_COUNT,
};

static const mouse_btn_t MOUSE_BTNS[M_COUNT] = {
    [M_LCLICK] = {"L CLICK", -56, 120, 92, 50, -1, M_SCRL_UP, -1, M_RCLICK},
    [M_RCLICK] = {"R CLICK", 56, 120, 92, 50, -1, M_SCRL_UP, M_LCLICK, -1},
    [M_SCRL_UP] = {LV_SYMBOL_UP "  SCROLL", 0, 182, 140, 40, M_LCLICK, M_SCRL_DN, -1, -1},
    [M_SCRL_DN] = {LV_SYMBOL_DOWN "  SCROLL", 0, 228, 140, 40, M_SCRL_UP, -1, -1, -1},
};

static lv_obj_t *s_mouse_screen = NULL;
static lv_obj_t *s_mouse_objs[M_COUNT];
static lv_timer_t *s_mouse_timer = NULL;
static int s_mouse_focus = M_LCLICK;

static bool s_m_up_last = false;
static bool s_m_down_last = false;
static bool s_m_left_last = false;
static bool s_m_right_last = false;
static bool s_m_ok_last = false;
static bool s_m_back_last = false;

static void mouse_apply_focus(lv_obj_t *btn, bool focused) {
  lv_obj_set_style_border_color(
      btn, focused ? current_theme.border_accent : current_theme.border_interface, 0);
  lv_obj_set_style_border_width(btn, focused ? 3 : 2, 0);

  lv_obj_set_style_shadow_width(btn, focused ? 14 : 0, 0);
  lv_obj_set_style_shadow_color(btn, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_spread(btn, focused ? 1 : 0, 0);
}

static void mouse_set_focus(int idx) {
  if (idx < 0 || idx >= M_COUNT || idx == s_mouse_focus)
    return;
  mouse_apply_focus(s_mouse_objs[s_mouse_focus], false);
  s_mouse_focus = idx;
  mouse_apply_focus(s_mouse_objs[s_mouse_focus], true);
}

static lv_obj_t *mouse_make_button(const mouse_btn_t *def) {
  lv_obj_t *btn = lv_obj_create(s_mouse_screen);
  lv_obj_set_size(btn, def->w, def->h);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, def->dx, def->dy);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(btn, (def->w == def->h) ? LV_RADIUS_CIRCLE : 14, 0);
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

static void mouse_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_mouse_screen) {
    lv_timer_delete(t);
    s_mouse_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = up_button_is_down();
  bool down = down_button_is_down();
  bool left = left_button_is_down();
  bool right = right_button_is_down();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (back && !s_m_back_last)
    ui_switch_screen(SCREEN_BLE_MENU);

  if (up && !s_m_up_last)
    mouse_set_focus(MOUSE_BTNS[s_mouse_focus].up);
  if (down && !s_m_down_last)
    mouse_set_focus(MOUSE_BTNS[s_mouse_focus].down);
  if (left && !s_m_left_last)
    mouse_set_focus(MOUSE_BTNS[s_mouse_focus].left);
  if (right && !s_m_right_last)
    mouse_set_focus(MOUSE_BTNS[s_mouse_focus].right);

  if (ok && !s_m_ok_last)
    ESP_LOGI(TAG, "press: %s", MOUSE_BTNS[s_mouse_focus].text);

  s_m_up_last = up;
  s_m_down_last = down;
  s_m_left_last = left;
  s_m_right_last = right;
  s_m_ok_last = ok;
  s_m_back_last = back;
}

void ui_ble_mouse_open(void) {
  if (s_mouse_screen != NULL) {
    lv_obj_del(s_mouse_screen);
    s_mouse_screen = NULL;
  }

  s_mouse_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_mouse_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_mouse_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_mouse_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_mouse_screen, 0, 0);
  lv_obj_set_style_pad_all(s_mouse_screen, 0, 0);

  ui_chrome_header(s_mouse_screen, "MouseAir", "/assets/icons/mouse_icon.bin");

  lv_obj_t *pad = lv_obj_create(s_mouse_screen);
  lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pad, 56, 56);
  lv_obj_align(pad, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_radius(pad, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pad, current_theme.bg_item_bot, 0);
  lv_obj_set_style_bg_grad_color(pad, current_theme.bg_item_top, 0);
  lv_obj_set_style_bg_grad_dir(pad, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(pad, 2, 0);
  lv_obj_set_style_border_color(pad, current_theme.border_accent, 0);

  lv_obj_t *pad_sym = lv_label_create(pad);
  lv_label_set_text(pad_sym, LV_SYMBOL_GPS);
  lv_obj_set_style_text_color(pad_sym, current_theme.border_accent, 0);
  lv_obj_center(pad_sym);

  for (int i = 0; i < M_COUNT; i++)
    s_mouse_objs[i] = mouse_make_button(&MOUSE_BTNS[i]);

  s_mouse_focus = M_LCLICK;
  mouse_apply_focus(s_mouse_objs[s_mouse_focus], true);

  ui_chrome_footer(s_mouse_screen, "Arrows Move   OK Press   BACK Exit");

  if (s_mouse_timer == NULL)
    s_mouse_timer = lv_timer_create(mouse_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load(s_mouse_screen);
}

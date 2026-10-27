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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "st7789.h"

#include "ble_hid_keyboard.h"
#include "buttons_gpio.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "BLE_MOUSE_UI";

#define NAV_TIMER_INTERVAL_MS 50
#define CONNECTION_POLL_US    500000
#define FOUND_DWELL_US        600000
#define DOT_CYCLE_US          350000
#define RIGHT_CLICK_HOLD_US   700000
#define SCROLL_REPEAT_US      140000
#define MOUSE_MOVE_STEP       7

#define COL_RAISE 0x170A28

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void blink_loop(lv_obj_t *obj, int32_t from, int32_t to, uint32_t ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_playback_duration(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static lv_obj_t *make_screen_root(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  return scr;
}

enum { PAIR_WAIT, PAIR_FOUND };

static lv_obj_t *s_pair_screen = NULL;
static lv_obj_t *s_pair_waves = NULL;
static lv_obj_t *s_pair_status = NULL;
static lv_timer_t *s_pair_timer = NULL;
static int64_t s_pair_start = 0;
static int64_t s_pair_found_at = 0;
static int64_t s_pair_last_query = 0;
static int s_pair_state = PAIR_WAIT;
static bool s_pair_back_last = false;

static void pair_reveal(void) {
  s_pair_state = PAIR_FOUND;
  s_pair_found_at = esp_timer_get_time();

  if (s_pair_waves) {
    lv_obj_del(s_pair_waves);
    s_pair_waves = NULL;
  }
  s_pair_waves = waves_create(s_pair_screen, LV_ALIGN_CENTER, 0, -24, LV_SYMBOL_OK, NULL);

  lv_label_set_text(s_pair_status, "Connected!");
  lv_obj_set_style_text_color(s_pair_status, lv_color_hex(UI_COL_SUCCESS), 0);
  ui_feedback(UI_FB_EMULATE);
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
    ble_hid_deinit();
    ui_switch_screen(SCREEN_BLE_MENU);
    return;
  }
  s_pair_back_last = back;

  int64_t now = esp_timer_get_time();
  if (s_pair_state == PAIR_WAIT) {
    int dots = (int)((now - s_pair_start) / DOT_CYCLE_US) % 4;
    lv_label_set_text(s_pair_status,
                      dots == 1   ? "Pairing."
                      : dots == 2 ? "Pairing.."
                      : dots == 3 ? "Pairing..."
                                  : "Pairing");
    if (now - s_pair_last_query >= CONNECTION_POLL_US) {
      s_pair_last_query = now;
      if (ble_hid_is_connected())
        pair_reveal();
    }
  } else {
    if (now - s_pair_found_at >= FOUND_DWELL_US)
      ui_switch_screen(SCREEN_BLE_MOUSE);
  }
}

void ui_ble_mouse_pairing_open(void) {
  if (s_pair_screen != NULL) {
    lv_obj_del(s_pair_screen);
    s_pair_screen = NULL;
  }

  s_pair_screen = make_screen_root();
  ui_chrome_header(s_pair_screen, "MouseAir", "/assets/icons/mouse.bin");

  s_pair_waves = waves_create(s_pair_screen, LV_ALIGN_CENTER, 0, -24, LV_SYMBOL_BLUETOOTH, NULL);

  s_pair_status = lv_label_create(s_pair_screen);
  lv_label_set_text(s_pair_status, "Pairing");
  lv_obj_set_style_text_color(s_pair_status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_pair_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_pair_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_pair_status, LV_ALIGN_CENTER, 0, 70);

  ui_chrome_footer(s_pair_screen, "BACK  Cancel");

  s_pair_state = PAIR_WAIT;
  s_pair_start = esp_timer_get_time();
  s_pair_found_at = 0;
  s_pair_last_query = 0;
  s_pair_back_last = false;
  if (ble_hid_init() != ESP_OK) {
    lv_label_set_text(s_pair_status, "HID init failed (C5?)");
    lv_obj_set_style_text_color(s_pair_status, lv_color_hex(UI_COL_DANGER), 0);
  }
  if (s_pair_timer == NULL)
    s_pair_timer = lv_timer_create(pair_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load_owned(&s_pair_screen, s_pair_screen);
}

typedef struct {
  const char *text;
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
    [M_LCLICK] = {"L CLICK", -1, M_SCRL_UP, -1, M_RCLICK},
    [M_RCLICK] = {"R CLICK", -1, M_SCRL_UP, M_LCLICK, -1},
    [M_SCRL_UP] = {LV_SYMBOL_UP "  SCROLL", M_LCLICK, M_SCRL_DN, -1, -1},
    [M_SCRL_DN] = {LV_SYMBOL_DOWN "  SCROLL", M_SCRL_UP, -1, -1, -1},
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
static int64_t s_ok_down_at = 0;
static int64_t s_last_scroll_at = 0;
static int64_t s_last_conn_query = 0;
static bool s_ok_right_sent = false;
static bool s_ok_chord = false;

static void mouse_apply_focus(lv_obj_t *btn, bool focused) {
  lv_obj_set_style_border_color(
      btn, focused ? current_theme.border_accent : current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(btn, focused ? 2 : 1, 0);
  lv_obj_set_style_bg_color(btn, focused ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(btn, focused ? LV_OPA_COVER : LV_OPA_80, 0);
  lv_obj_set_style_shadow_width(btn, focused ? 14 : 0, 0);
  lv_obj_set_style_shadow_color(btn, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(btn, focused ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_spread(btn, focused ? -3 : 0, 0);

  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  if (lbl)
    lv_obj_set_style_text_color(
        lbl, focused ? current_theme.border_accent : current_theme.text_main, 0);
}

static void mouse_set_focus(int idx) {
  if (idx < 0 || idx >= M_COUNT || idx == s_mouse_focus)
    return;
  mouse_apply_focus(s_mouse_objs[s_mouse_focus], false);
  s_mouse_focus = idx;
  mouse_apply_focus(s_mouse_objs[s_mouse_focus], true);
  ui_feedback(UI_FB_NAV);
}

static lv_obj_t *mouse_make_button(lv_obj_t *parent, const char *text) {
  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_bg_color(btn, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(lbl);
  return btn;
}

static void mouse_click(uint8_t button_mask) {
  ble_hid_send_mouse(0, 0, button_mask, 0);
  vTaskDelay(pdMS_TO_TICKS(18));
  ble_hid_send_mouse(0, 0, 0, 0);
}

static void mouse_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_mouse_screen) {
    lv_timer_delete(t);
    s_mouse_timer = NULL;
    return;
  }

  bool up = up_button_is_down();
  bool down = down_button_is_down();
  bool left = left_button_is_down();
  bool right = right_button_is_down();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();
  int64_t now = esp_timer_get_time();

  if (ui_input_is_locked()) {
    s_m_up_last = up;
    s_m_down_last = down;
    s_m_left_last = left;
    s_m_right_last = right;
    s_m_ok_last = ok;
    s_m_back_last = back;
    return;
  }

  if (back && !s_m_back_last) {
    ble_hid_send_mouse(0, 0, 0, 0);
    ble_hid_deinit();
    ui_switch_screen(SCREEN_BLE_MENU);
    return;
  }

  if (now - s_last_conn_query >= 1000000) {
    s_last_conn_query = now;
    if (!ble_hid_is_connected()) {
      notify(NOTIFY_WARNING, "HID mouse disconnected");
      ui_switch_screen(SCREEN_BLE_MOUSE_PAIRING);
      return;
    }
  }

  if (ok && !s_m_ok_last) {
    s_ok_down_at = now;
    s_ok_right_sent = false;
    s_ok_chord = false;
    s_last_scroll_at = 0;
  }

  if (ok && (up || down)) {
    s_ok_chord = true;
    if (s_last_scroll_at == 0 || now - s_last_scroll_at >= SCROLL_REPEAT_US) {
      s_last_scroll_at = now;
      int8_t wheel = up ? 1 : -1;
      ble_hid_send_mouse(0, 0, 0, wheel);
      mouse_set_focus(up ? M_SCRL_UP : M_SCRL_DN);
    }
  } else if (ok && !s_ok_right_sent && !s_ok_chord && now - s_ok_down_at >= RIGHT_CLICK_HOLD_US) {
    mouse_click(0x02);
    s_ok_right_sent = true;
    mouse_set_focus(M_RCLICK);
    ui_feedback(UI_FB_SELECT);
  } else if (!ok) {
    int8_t dx = 0;
    int8_t dy = 0;
    if (left)
      dx -= MOUSE_MOVE_STEP;
    if (right)
      dx += MOUSE_MOVE_STEP;
    if (up)
      dy -= MOUSE_MOVE_STEP;
    if (down)
      dy += MOUSE_MOVE_STEP;
    if (dx != 0 || dy != 0)
      ble_hid_send_mouse(dx, dy, 0, 0);
  }

  if (!ok && s_m_ok_last) {
    if (!s_ok_right_sent && !s_ok_chord) {
      mouse_click(0x01);
      mouse_set_focus(M_LCLICK);
      ui_feedback(UI_FB_SELECT);
    }
    s_ok_down_at = 0;
  }

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
  s_m_up_last = s_m_down_last = s_m_left_last = false;
  s_m_right_last = s_m_ok_last = s_m_back_last = false;
  s_ok_down_at = 0;
  s_last_scroll_at = 0;
  s_last_conn_query = esp_timer_get_time();
  s_ok_right_sent = false;
  s_ok_chord = false;

  s_mouse_screen = make_screen_root();
  ui_chrome_header(s_mouse_screen, "MouseAir", "/assets/icons/mouse.bin");

  lv_obj_t *body = lv_obj_create(s_mouse_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(body, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 10, 0);
  lv_obj_set_style_pad_row(body, 8, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *pad = lv_obj_create(body);
  lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pad, 200, 84);
  lv_obj_set_style_radius(pad, 14, 0);
  lv_obj_set_style_bg_color(pad, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(pad, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(pad, 1, 0);
  lv_obj_set_style_border_color(pad, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(pad, 16, 0);
  lv_obj_set_style_shadow_color(pad, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(pad, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(pad, -4, 0);
  lv_obj_set_style_pad_all(pad, 0, 0);

  lv_obj_t *cross = lv_label_create(pad);
  lv_label_set_text(cross, LV_SYMBOL_GPS);
  lv_obj_set_style_text_color(cross, current_theme.border_accent, 0);
  lv_obj_center(cross);
  blink_loop(cross, LV_OPA_COVER, 120, 900);

  lv_obj_t *row = lv_obj_create(body);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, 44);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  s_mouse_objs[M_LCLICK] = mouse_make_button(row, MOUSE_BTNS[M_LCLICK].text);
  lv_obj_set_size(s_mouse_objs[M_LCLICK], lv_pct(48), 44);
  s_mouse_objs[M_RCLICK] = mouse_make_button(row, MOUSE_BTNS[M_RCLICK].text);
  lv_obj_set_size(s_mouse_objs[M_RCLICK], lv_pct(48), 44);

  s_mouse_objs[M_SCRL_UP] = mouse_make_button(body, MOUSE_BTNS[M_SCRL_UP].text);
  lv_obj_set_size(s_mouse_objs[M_SCRL_UP], lv_pct(100), 40);
  s_mouse_objs[M_SCRL_DN] = mouse_make_button(body, MOUSE_BTNS[M_SCRL_DN].text);
  lv_obj_set_size(s_mouse_objs[M_SCRL_DN], lv_pct(100), 40);

  s_mouse_focus = M_LCLICK;
  mouse_apply_focus(s_mouse_objs[s_mouse_focus], true);

  ui_chrome_footer(s_mouse_screen, "Arrows move  OK click/hold right  BACK exit");

  if (s_mouse_timer == NULL)
    s_mouse_timer = lv_timer_create(mouse_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load_owned(&s_mouse_screen, s_mouse_screen);
}

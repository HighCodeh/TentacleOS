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

#include "ble_keyboard_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "ble_hid_keyboard.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "BLE_KEYBOARD_UI";

#define CONN_POLL_MS    400
#define CURSOR_BLINK_MS 500

#define HID_KEY_ENTER 0x28
#define HID_KEY_SPACE 0x2C
#define HID_MOD_SHIFT 0x02
#define HID_MOD_GUI   0x08

#define KB_ICON "/assets/icons/keyboard.bin"

#define CONSOLE_W 212
#define CONSOLE_H 150
#define TYPED_MAX 160

static const char CANNED_TEXT[] = "notepad.exe\nHello from TentacleOS!\n";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_body = NULL;
static lv_obj_t *s_footer = NULL;
static lv_obj_t *s_term_label = NULL;
static lv_timer_t *s_phase_timer = NULL;
static lv_timer_t *s_cursor_timer = NULL;

static char s_typed[TYPED_MAX];
static int s_type_pos = 0;
static bool s_connected = false;
static bool s_cursor_on = true;

static void keyboard_input(const input_event_t *ev, void *ctx);
static void phase_timer_cb(lv_timer_t *timer);
static void cursor_timer_cb(lv_timer_t *timer);
static void build_console(void);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, 10, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, 18, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  lv_obj_set_style_pad_all(p, 8, 0);
  return p;
}

static lv_obj_t *make_body(lv_obj_t *parent) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(b, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(b, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  return b;
}

static void build_pairing(void) {
  waves_create(s_body, LV_ALIGN_CENTER, 0, -34, NULL, KB_ICON);

  lv_obj_t *status = lv_label_create(s_body);
  lv_label_set_text(status, "Pairing as HID keyboard...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, 58);

  lv_obj_t *hint = lv_label_create(s_body);
  lv_label_set_text(hint, "Accept on Target-PC");
  lv_obj_set_style_text_color(hint, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 82);

  fade_in(status, 220);
  fade_in(hint, 260);
}

static void update_terminal(void) {
  if (s_term_label == NULL)
    return;
  char buf[TYPED_MAX + 2];
  snprintf(buf, sizeof(buf), "%s%s", s_typed, s_cursor_on ? "_" : " ");
  lv_label_set_text(s_term_label, buf);
}

static void build_console(void) {
  lv_obj_clean(s_body);
  s_term_label = NULL;
  s_type_pos = 0;
  s_cursor_on = true;
  s_typed[0] = '\0';
  s_connected = true;

  lv_obj_t *status = lv_label_create(s_body);
  lv_label_set_text(status, LV_SYMBOL_OK "  Connected");
  lv_obj_set_style_text_color(status, lv_color_hex(UI_COL_SUCCESS), 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *card = lit_panel(s_body, CONSOLE_W, CONSOLE_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 44);

  s_term_label = lv_label_create(card);
  lv_label_set_long_mode(s_term_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_term_label, CONSOLE_W - 20);
  lv_obj_set_style_text_color(s_term_label, lv_color_hex(UI_COL_SUCCESS), 0);
  lv_obj_set_style_text_font(s_term_label, &lv_font_montserrat_14, 0);
  lv_obj_align(s_term_label, LV_ALIGN_TOP_LEFT, 0, 0);
  update_terminal();

  fade_in(status, 220);
  fade_in(card, 260);

  ui_feedback(UI_FB_READ);
  notify(NOTIFY_INFO, "HID keyboard connected");
  ui_chrome_footer_set_text(s_footer, "OK Key   L GUI   R Enter   BACK Exit");

  if (s_cursor_timer == NULL)
    s_cursor_timer = lv_timer_create(cursor_timer_cb, CURSOR_BLINK_MS, NULL);
}

static bool ascii_to_hid(char c, uint8_t *key, uint8_t *mod) {
  *mod = 0;
  if (c >= 'a' && c <= 'z') {
    *key = 0x04 + (c - 'a');
    return true;
  }
  if (c >= 'A' && c <= 'Z') {
    *key = 0x04 + (c - 'A');
    *mod = HID_MOD_SHIFT;
    return true;
  }
  if (c >= '1' && c <= '9') {
    *key = 0x1E + (c - '1');
    return true;
  }
  switch (c) {
    case '0':
      *key = 0x27;
      return true;
    case '\n':
      *key = HID_KEY_ENTER;
      return true;
    case ' ':
      *key = HID_KEY_SPACE;
      return true;
    case '.':
      *key = 0x37;
      return true;
    case ',':
      *key = 0x36;
      return true;
    case '-':
      *key = 0x2D;
      return true;
    case '_':
      *key = 0x2D;
      *mod = HID_MOD_SHIFT;
      return true;
    case '/':
      *key = 0x38;
      return true;
    case ':':
      *key = 0x33;
      *mod = HID_MOD_SHIFT;
      return true;
    case ';':
      *key = 0x33;
      return true;
    case '!':
      *key = 0x1E;
      *mod = HID_MOD_SHIFT;
      return true;
    case '?':
      *key = 0x38;
      *mod = HID_MOD_SHIFT;
      return true;
    default:
      return false;
  }
}

static void type_char(char c) {
  uint8_t key, mod;
  if (ascii_to_hid(c, &key, &mod))
    ble_hid_send_key(key, mod);
  if (s_type_pos >= TYPED_MAX - 2)
    return;
  s_typed[s_type_pos++] = c;
  s_typed[s_type_pos] = '\0';
  update_terminal();
}

void ui_ble_keyboard_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_term_label = NULL;
  s_type_pos = 0;
  s_connected = false;
  s_cursor_on = true;
  s_typed[0] = '\0';
  s_phase_timer = NULL;
  s_cursor_timer = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "BLE KEYBOARD", KB_ICON);
  s_footer = ui_chrome_footer(s_screen, "BACK  Cancel");

  s_body = make_body(s_screen);
  build_pairing();

  ui_input_set_screen_handler(keyboard_input, NULL);

  if (ble_hid_init() != ESP_OK) {
    lv_obj_t *err = lv_label_create(s_body);
    lv_label_set_text(err, "HID init failed (C5?)");
    lv_obj_set_style_text_color(err, current_theme.text_main, 0);
    lv_obj_align(err, LV_ALIGN_CENTER, 0, 58);
    ESP_LOGE(TAG, "ble_hid_init failed");
  } else {
    s_phase_timer = lv_timer_create(phase_timer_cb, CONN_POLL_MS, NULL);
  }

  ui_feedback(UI_FB_EMULATE);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void phase_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_phase_timer == timer)
      s_phase_timer = NULL;
    return;
  }
  if (ble_hid_is_connected()) {
    lv_timer_delete(timer);
    s_phase_timer = NULL;
    build_console();
  }
}

static void cursor_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen || !s_connected) {
    lv_timer_delete(timer);
    s_cursor_timer = NULL;
    return;
  }
  s_cursor_on = !s_cursor_on;
  update_terminal();
}

static void stop_timers(void) {
  if (s_phase_timer != NULL) {
    lv_timer_delete(s_phase_timer);
    s_phase_timer = NULL;
  }
  if (s_cursor_timer != NULL) {
    lv_timer_delete(s_cursor_timer);
    s_cursor_timer = NULL;
  }
}

static void keyboard_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        stop_timers();
        ble_hid_deinit();
        s_connected = false;
        ui_switch_screen(SCREEN_BLE_MENU);
      }
      break;
    case INPUT_BTN_OK:
      if (press && s_connected) {
        if (CANNED_TEXT[s_type_pos] != '\0') {
          type_char(CANNED_TEXT[s_type_pos]);
          ui_feedback(UI_FB_NAV);
        } else {
          notify(NOTIFY_SAVED, "Payload typed");
        }
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press && s_connected) {
        type_char('\n');
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_LEFT:
      if (press && s_connected) {
        ble_hid_send_key(0, HID_MOD_GUI);
        ui_feedback(UI_FB_NAV);
      }
      break;
    default:
      break;
  }
}

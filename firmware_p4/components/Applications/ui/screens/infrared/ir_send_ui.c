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

#include "ir_send_ui.h"

#include <stdio.h>

#include "esp_log.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "sigwave_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "IR_SEND_UI";

#define SIG_GREEN 0x00E676

#define HEADER_TITLE_Y     10
#define HEADER_RULE_Y      32
#define HEADER_RULE_W      70
#define HEADER_RULE_H      2
#define HEADER_RULE_RADIUS 1

#define STATUS_Y   50
#define DETAIL_Y   68
#define HINT_Y_OFS -6

#define NAV_TIMER_MS 50
#define SENDING_MS   1600
#define DOT_CYCLE_MS 350

#define IR_ICON "/assets/icons/ir_icon.bin"

#define STATUS_SENDING "Sending"
#define STATUS_SENT    "Signal sent!"

#define HINT_SENDING "Transmitting..."
#define HINT_SENT    "RIGHT = Resend   BACK = Exit"

static const char *MOCK_SIGNALS[] = {
    "[NEC] TV Power",
    "[NEC] TV Vol +",
    "[NEC] TV Vol -",
    "[SAMSUNG] Soundbar",
    "[RC5] Set-top Box",
    "[AC] Cool 22C",
};
#define MOCK_SIGNALS_COUNT ((int)(sizeof(MOCK_SIGNALS) / sizeof(MOCK_SIGNALS[0])))

typedef enum {
  VIEW_LIST = 0,
  VIEW_SENDING,
  VIEW_SENT,
} send_view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static send_view_t s_view = VIEW_LIST;
static int s_sel = 0;

static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_send_timer = NULL;

static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static uint32_t s_send_start = 0;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *t);
static void build_list(void);
static void build_sending(void);
static void send_done_cb(lv_timer_t *t);

static void stop_send_timer(void) {
  if (s_send_timer != NULL) {
    lv_timer_delete(s_send_timer);
    s_send_timer = NULL;
  }
}

static void reset_latch(void) {
  s_btn_up_last = false;
  s_btn_down_last = false;
  s_btn_left_last = false;
  s_btn_right_last = false;
  s_btn_ok_last = false;
  s_btn_back_last = false;
}

static lv_obj_t *new_screen(void) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  return screen;
}

static void build_header(const char *text) {
  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, text);
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, HEADER_TITLE_Y);

  lv_obj_t *rule = lv_obj_create(s_screen);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(rule, lv_pct(HEADER_RULE_W), HEADER_RULE_H);
  lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, HEADER_RULE_Y);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, HEADER_RULE_RADIUS, 0);
  lv_obj_set_style_bg_color(rule, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);
}

static lv_obj_t *make_hint(const char *text) {
  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, text);
  lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(hint, LV_OPA_60, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, HINT_Y_OFS);
  return hint;
}

static void set_status(const char *text, bool success) {
  if (s_status_label == NULL)
    return;
  lv_label_set_text(s_status_label, text);
  lv_obj_set_style_text_color(
      s_status_label, success ? lv_color_hex(SIG_GREEN) : current_theme.text_main, 0);
}

static void build_list(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_label = NULL;
  s_hint_label = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "IR SEND", "/assets/icons/ir_send_menu_icon.bin");
  for (int i = 0; i < MOCK_SIGNALS_COUNT; i++)
    menu_component_add_item(&s_menu, IR_ICON, MOCK_SIGNALS[i]);
  if (s_sel > 0 && s_sel < MOCK_SIGNALS_COUNT)
    menu_component_select(&s_menu, s_sel);

  ui_screen_load(s_screen);
}

static void build_sending(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = new_screen();
  ui_chrome_header(s_screen, "Send", "/assets/icons/ir_send_menu_icon.bin");

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, STATUS_SENDING);
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  lv_obj_t *which = lv_label_create(s_screen);
  lv_label_set_text(which, MOCK_SIGNALS[s_sel]);
  lv_obj_set_style_text_color(which, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(which, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(which, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(which, LV_ALIGN_TOP_MID, 0, DETAIL_Y);

  waves_create(s_screen, LV_ALIGN_CENTER, 0, 6, NULL, IR_ICON);
  sigwave_create(s_screen, LV_ALIGN_BOTTOM_MID, 0, -28);

  s_hint_label = ui_chrome_footer(s_screen, HINT_SENDING);

  ui_screen_load(s_screen);
}

static void show_sent(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = new_screen();
  ui_chrome_header(s_screen, "Send", "/assets/icons/ir_send_menu_icon.bin");

  waves_create(s_screen, LV_ALIGN_CENTER, 0, 6, LV_SYMBOL_OK, NULL);

  s_status_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y);
  set_status(STATUS_SENT, true);

  lv_obj_t *which = lv_label_create(s_screen);
  lv_label_set_text(which, MOCK_SIGNALS[s_sel]);
  lv_obj_set_style_text_color(which, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(which, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(which, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(which, LV_ALIGN_TOP_MID, 0, DETAIL_Y);

  s_hint_label = ui_chrome_footer(s_screen, HINT_SENT);

  ui_screen_load(s_screen);
}

static void start_send(void) {
  stop_send_timer();
  s_view = VIEW_SENDING;
  s_send_start = lv_tick_get();
  ESP_LOGI(TAG, "mock send: %s", MOCK_SIGNALS[s_sel]);
  build_sending();
  s_send_timer = lv_timer_create(send_done_cb, SENDING_MS, NULL);
  lv_timer_set_repeat_count(s_send_timer, 1);
}

static void send_done_cb(lv_timer_t *t) {
  (void)t;
  s_send_timer = NULL;
  if (lv_screen_active() != s_screen)
    return;
  s_view = VIEW_SENT;
  ui_feedback(UI_FB_WRITE);
  show_sent();
}

static void sending_tick(void) {
  if (s_status_label == NULL)
    return;
  int dots = ((lv_tick_get() - s_send_start) / DOT_CYCLE_MS) % 4;
  char buf[24];
  snprintf(buf,
           sizeof(buf),
           "%s%s",
           STATUS_SENDING,
           dots == 1   ? "."
           : dots == 2 ? ".."
           : dots == 3 ? "..."
                       : "");
  lv_label_set_text(s_status_label, buf);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  if (s_view == VIEW_SENDING)
    sending_tick();

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  switch (s_view) {
    case VIEW_LIST:
      if (down && !s_btn_down_last)
        menu_component_next(&s_menu);
      if (up && !s_btn_up_last)
        menu_component_prev(&s_menu);
      if ((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) {
        s_sel = menu_component_get_selected(&s_menu);
        start_send();
        reset_latch();
        return;
      }
      if ((back && !s_btn_back_last) || (left && !s_btn_left_last))
        ui_switch_screen(SCREEN_IR_MENU);
      break;

    case VIEW_SENDING:
      if (back && !s_btn_back_last) {
        stop_send_timer();
        s_view = VIEW_LIST;
        build_list();
        reset_latch();
        return;
      }
      break;

    case VIEW_SENT:
      if (right && !s_btn_right_last) {
        start_send();
        reset_latch();
        return;
      }
      if (back && !s_btn_back_last) {
        s_view = VIEW_LIST;
        build_list();
        reset_latch();
        return;
      }
      break;

    default:
      break;
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_ir_send_open(void) {
  stop_send_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_label = NULL;
  s_hint_label = NULL;
  s_view = VIEW_LIST;
  s_sel = 0;
  reset_latch();

  build_list();

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
}

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

#include "ir_receive_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "capture_result_ui.h"
#include "notify_ui.h"
#include "sigwave_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "IR_RX_UI";

#define SIG_GREEN 0x00E676

#define HEADER_TITLE_Y     10
#define HEADER_RULE_Y      32
#define HEADER_RULE_W      70
#define HEADER_RULE_H      2
#define HEADER_RULE_RADIUS 1

#define STATUS_Y 48
#define DETAIL_Y 66

#define NAV_TIMER_MS 50
#define CAPTURE_MS   2200
#define DOT_CYCLE_MS 350

#define CARD_W       162
#define CARD_H       82
#define CARD_RADIUS  12
#define CARD_BORDER  2
#define CARD_Y_OFS   -28
#define CARD_RISE_PX 70
#define CARD_RISE_MS 450

#define IR_ICON "/assets/icons/settings_input_antenna.bin"

#define STATUS_IDLE     "Press OK to start"
#define STATUS_BUSY     "Waiting for signal"
#define STATUS_CAPTURED "Signal captured!"
#define STATUS_SAVED    "Signal saved!"

#define DETAIL_AIM "Point remote at device"
#define CARD_INFO  LV_SYMBOL_OK "  NEC   0x04 / 0x08"

#define HINT_IDLE     "OK = Capture   BACK = Exit"
#define HINT_BUSY     "BACK to cancel"
#define HINT_SHOW     "BACK = Exit"
#define HINT_CAPTURED "UP/DOWN choose   OK do   BACK exit"

#define REVEAL_MS 3000

#define WAVES_IDLE_OPA LV_OPA_40

typedef enum {
  ST_IDLE = 0,
  ST_CAPTURING,
  ST_CAPTURED,
  ST_OPTIONS,
} rx_state_t;

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_capture_timer = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_detail_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_waves = NULL;
static lv_obj_t *s_sig = NULL;
static lv_obj_t *s_card = NULL;
static capture_result_t s_cr = {0};
static rx_state_t s_state = ST_IDLE;
static uint32_t s_capture_start = 0;
static uint32_t s_captured_at = 0;
static bool s_saved = false;

static bool s_btn_back_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_right_last = false;
static bool s_btn_up_last = false;
static bool s_btn_down_last = false;

static void nav_timer_cb(lv_timer_t *timer);
static void capture_done_cb(lv_timer_t *timer);

static void stop_capture_timer(void) {
  if (s_capture_timer != NULL) {
    lv_timer_delete(s_capture_timer);
    s_capture_timer = NULL;
  }
}

static void clear_result(void) {
  if (s_card != NULL) {
    lv_obj_del(s_card);
    s_card = NULL;
  }
  capture_result_destroy(&s_cr);
}

static void card_rise_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void set_status(const char *text, bool success) {
  if (s_status_label == NULL)
    return;
  lv_label_set_text(s_status_label, text);
  lv_obj_set_style_text_color(
      s_status_label, success ? lv_color_hex(SIG_GREEN) : current_theme.text_main, 0);
}

static void set_hint(const char *text) {
  if (s_hint_label != NULL)
    ui_chrome_footer_set_text(s_hint_label, text);
}

static void start_capture(void) {
  clear_result();
  s_state = ST_CAPTURING;
  s_saved = false;
  s_capture_start = lv_tick_get();
  if (s_status_label)
    lv_obj_remove_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
  if (s_detail_label)
    lv_obj_remove_flag(s_detail_label, LV_OBJ_FLAG_HIDDEN);
  set_status(STATUS_BUSY, false);
  if (s_detail_label)
    lv_label_set_text(s_detail_label, DETAIL_AIM);
  if (s_waves) {
    lv_obj_remove_flag(s_waves, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_waves, LV_OPA_COVER, 0);
  }
  if (s_sig)
    lv_obj_remove_flag(s_sig, LV_OBJ_FLAG_HIDDEN);
  set_hint(HINT_BUSY);

  s_capture_timer = lv_timer_create(capture_done_cb, CAPTURE_MS, NULL);
  lv_timer_set_repeat_count(s_capture_timer, 1);
}

static void capture_done_cb(lv_timer_t *timer) {
  (void)timer;
  s_capture_timer = NULL;
  if (lv_screen_active() != s_screen)
    return;

  s_state = ST_CAPTURED;
  if (s_waves)
    lv_obj_add_flag(s_waves, LV_OBJ_FLAG_HIDDEN);
  if (s_sig)
    lv_obj_add_flag(s_sig, LV_OBJ_FLAG_HIDDEN);
  set_status(STATUS_CAPTURED, true);
  if (s_detail_label)
    lv_label_set_text(s_detail_label, "");

  s_card = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_card, CARD_W, CARD_H);
  lv_obj_align(s_card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
  lv_obj_set_style_radius(s_card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_card, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(s_card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(s_card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(s_card, CARD_BORDER, 0);
  lv_obj_set_style_border_color(s_card, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(s_card, 6, 0);

  lv_obj_t *info = lv_label_create(s_card);
  lv_label_set_text(info, CARD_INFO);
  lv_obj_set_style_text_color(info, current_theme.text_main, 0);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
  lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 0);

  sigwave_create_static(s_card, LV_ALIGN_BOTTOM_MID, 0, -2);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_card);
  lv_anim_set_exec_cb(&a, card_rise_cb);
  lv_anim_set_values(&a, CARD_RISE_PX, 0);
  lv_anim_set_duration(&a, CARD_RISE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  s_captured_at = lv_tick_get();
  set_hint(HINT_SHOW);
  ESP_LOGI(TAG, "mock capture done");
  ui_feedback(UI_FB_READ);
}

static void show_options(void) {
  if (s_card != NULL) {
    lv_obj_del(s_card);
    s_card = NULL;
  }
  if (s_status_label)
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
  if (s_detail_label)
    lv_obj_add_flag(s_detail_label, LV_OBJ_FLAG_HIDDEN);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = IR_ICON,
      .card_title = "Signal captured",
      .card_sub = "NEC protocol",
      .card_value = "cmd 0x04 / 0x08",
      .primary_label = "Send",
      .again_label = "Receive again",
  };
  s_cr = capture_result_create(s_screen, &cfg);
  s_state = ST_OPTIONS;
  set_hint(HINT_CAPTURED);
}

void ui_ir_receive_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  stop_capture_timer();
  s_card = NULL;
  s_cr = (capture_result_t){0};
  s_state = ST_IDLE;
  s_saved = false;
  s_btn_back_last = false;
  s_btn_ok_last = false;
  s_btn_right_last = false;
  s_btn_up_last = false;
  s_btn_down_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "Learn", "/assets/icons/settings_input_antenna.bin");

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, STATUS_IDLE);
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_detail_label = lv_label_create(s_screen);
  lv_label_set_text(s_detail_label, "");
  lv_obj_set_style_text_color(s_detail_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_detail_label, LV_ALIGN_TOP_MID, 0, DETAIL_Y);

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, 12, NULL, IR_ICON);
  lv_obj_set_style_opa(s_waves, WAVES_IDLE_OPA, 0);
  s_sig = sigwave_create(s_screen, LV_ALIGN_BOTTOM_MID, 0, -28);
  lv_obj_add_flag(s_sig, LV_OBJ_FLAG_HIDDEN);

  s_hint_label = ui_chrome_footer(s_screen, HINT_IDLE);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void capturing_tick(void) {
  if (s_status_label == NULL)
    return;
  int dots = ((lv_tick_get() - s_capture_start) / DOT_CYCLE_MS) % 4;
  char buf[24];
  snprintf(buf,
           sizeof(buf),
           "%s%s",
           STATUS_BUSY,
           dots == 1   ? "."
           : dots == 2 ? ".."
           : dots == 3 ? "..."
                       : "");
  lv_label_set_text(s_status_label, buf);
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  if (s_state == ST_CAPTURING)
    capturing_tick();

  bool is_back = back_button_is_down();
  bool is_ok = ok_button_is_down();
  bool is_right = ui_btn_right();
  bool is_up = ui_btn_up();
  bool is_down = ui_btn_down();

  if (is_back && !s_btn_back_last) {
    stop_capture_timer();
    ui_switch_screen(SCREEN_IR_MENU);
    return;
  }

  if (s_state == ST_IDLE) {
    if (is_ok && !s_btn_ok_last)
      start_capture();
  } else if (s_state == ST_CAPTURED) {
    if (lv_tick_get() - s_captured_at >= REVEAL_MS)
      show_options();
  } else if (s_state == ST_OPTIONS) {
    if (is_down && !s_btn_down_last) {
      capture_result_next(&s_cr);
      ui_feedback(UI_FB_NAV);
    }
    if (is_up && !s_btn_up_last) {
      capture_result_prev(&s_cr);
      ui_feedback(UI_FB_NAV);
    }
    if (is_ok && !s_btn_ok_last) {
      switch (capture_result_selected(&s_cr)) {
        case CAP_ACT_PRIMARY:
          ui_switch_screen(SCREEN_IR_SEND);
          return;
        case CAP_ACT_SAVE:
          if (!s_saved) {
            s_saved = true;
            capture_result_mark_saved(&s_cr);
            ESP_LOGI(TAG, "mock signal saved");
            ui_feedback(UI_FB_WRITE);
            notify(NOTIFY_SAVED, "IR signal saved");
          }
          break;
        case CAP_ACT_AGAIN:
          start_capture();
          break;
        case CAP_ACT_DISCARD:
          stop_capture_timer();
          ui_switch_screen(SCREEN_IR_MENU);
          return;
        default:
          break;
      }
    }
  }

  s_btn_back_last = is_back;
  s_btn_ok_last = is_ok;
  s_btn_right_last = is_right;
  s_btn_up_last = is_up;
  s_btn_down_last = is_down;
}

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

#include "capture_result_ui.h"
#include "ir_store.h"
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

#define POLL_TICK_MS       50
#define CAPTURE_WINDOW_MS  1500 // one ir_capture_start() window; re-armed while listening
#define DOT_CYCLE_MS       350

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
static lv_timer_t *s_tick_timer = NULL;
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
static bool s_listening = false;
static ir_data_t s_captured = {0};
static char s_card_text[48];
static rmt_symbol_word_t s_raw_buf[IR_MAX_SYMBOLS];

static void ir_receive_tick_cb(lv_timer_t *timer);
static void ir_receive_input(const input_event_t *ev, void *ctx);
static void build_captured_card(void);

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

static void stop_listening(void) {
  s_listening = false;
  ir_capture_reset();
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

  ir_capture_reset();
  ir_capture_start(CAPTURE_WINDOW_MS);
  s_listening = true;
}

// Populate the captured card from the real decoded frame in s_captured.
static void build_captured_card(void) {
  s_state = ST_CAPTURED;
  s_listening = false;
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
  if (s_captured.protocol != IR_PROTO_UNKNOWN)
    snprintf(s_card_text,
             sizeof(s_card_text),
             LV_SYMBOL_OK "  %s   0x%02lX / 0x%02lX",
             ir_protocol_name(s_captured.protocol),
             (unsigned long)s_captured.address,
             (unsigned long)s_captured.command);
  else
    snprintf(s_card_text, sizeof(s_card_text), LV_SYMBOL_OK "  RAW signal");
  lv_label_set_text(info, s_card_text);
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
  ESP_LOGI(TAG, "captured %s", ir_protocol_name(s_captured.protocol));
  ir_print_data(&s_captured);
  ui_feedback(UI_FB_READ);
}

// Re-transmit the captured signal — decoded frame, or raw fallback.
static void send_captured(void) {
  if (s_captured.protocol != IR_PROTO_UNKNOWN) {
    ir_store_send_data(&s_captured);
    return;
  }
  size_t n = 0;
  if (ir_get_last_raw(s_raw_buf, IR_MAX_SYMBOLS, &n) == ESP_OK && n > 0)
    ir_store_send_raw(s_raw_buf, n, IR_CARRIER_HZ_DEFAULT);
}

// Persist the captured signal to /sdcard/ir as a Flipper .ir file.
static bool save_captured(void) {
  char name[IR_STORE_NAME_MAX];
  if (s_captured.protocol != IR_PROTO_UNKNOWN) {
    ir_store_name_for_data(&s_captured, name, sizeof(name));
    return ir_store_save_data(name, &s_captured) == ESP_OK;
  }
  size_t n = 0;
  if (ir_get_last_raw(s_raw_buf, IR_MAX_SYMBOLS, &n) != ESP_OK || n == 0)
    return false;
  ir_store_next_free("raw", name, sizeof(name));
  return ir_store_save_raw(name, s_raw_buf, n, IR_CARRIER_HZ_DEFAULT) == ESP_OK;
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

  static char sub[24];
  static char val[32];
  if (s_captured.protocol != IR_PROTO_UNKNOWN) {
    snprintf(sub, sizeof(sub), "%s protocol", ir_protocol_name(s_captured.protocol));
    snprintf(val,
             sizeof(val),
             "cmd 0x%02lX / 0x%02lX",
             (unsigned long)s_captured.address,
             (unsigned long)s_captured.command);
  } else {
    snprintf(sub, sizeof(sub), "RAW capture");
    snprintf(val, sizeof(val), "unknown protocol");
  }

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = IR_ICON,
      .card_title = "Signal captured",
      .card_sub = sub,
      .card_value = val,
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
  stop_listening();
  s_card = NULL;
  s_cr = (capture_result_t){0};
  s_state = ST_IDLE;
  s_saved = false;
  s_captured = (ir_data_t){0};

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

  if (s_tick_timer == NULL)
    s_tick_timer = lv_timer_create(ir_receive_tick_cb, POLL_TICK_MS, NULL);
  ui_input_set_screen_handler(ir_receive_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
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

// Return the screen to its idle "Press OK" state (used on capture error).
static void back_to_idle(void) {
  stop_listening();
  s_state = ST_IDLE;
  set_status(STATUS_IDLE, false);
  if (s_detail_label)
    lv_label_set_text(s_detail_label, "");
  if (s_waves) {
    lv_obj_remove_flag(s_waves, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_waves, WAVES_IDLE_OPA, 0);
  }
  if (s_sig)
    lv_obj_add_flag(s_sig, LV_OBJ_FLAG_HIDDEN);
  set_hint(HINT_IDLE);
}

static void poll_capture(void) {
  capturing_tick();
  ir_data_t d;
  ir_cap_status_t st = ir_capture_poll(&d);
  if (st == IR_CAP_GOT) {
    s_captured = d;
    build_captured_card();
  } else if (st == IR_CAP_TIMEOUT && s_listening) {
    // Nothing yet — re-arm and keep listening until a signal or the user cancels.
    ir_capture_reset();
    ir_capture_start(CAPTURE_WINDOW_MS);
  } else if (st == IR_CAP_ERROR) {
    // RX channel couldn't be brought up — don't spin forever on "Waiting...".
    back_to_idle();
    notify(NOTIFY_WARNING, "IR receiver unavailable");
  }
}

static void ir_receive_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_tick_timer = NULL;
    return;
  }

  if (s_state == ST_CAPTURING) {
    poll_capture();
  } else if (s_state == ST_CAPTURED) {
    if (lv_tick_get() - s_captured_at >= REVEAL_MS)
      show_options();
  }
}

static void ir_receive_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (ev->button == INPUT_BTN_BACK) {
    if (press) {
      stop_listening();
      ui_switch_screen(SCREEN_IR_MENU);
    }
    return;
  }

  if (s_state == ST_IDLE) {
    if (ev->button == INPUT_BTN_OK && press)
      start_capture();
  } else if (s_state == ST_OPTIONS) {
    switch (ev->button) {
      case INPUT_BTN_DOWN:
        if (nav) {
          capture_result_next(&s_cr);
          ui_feedback(UI_FB_NAV);
        }
        break;
      case INPUT_BTN_UP:
        if (nav) {
          capture_result_prev(&s_cr);
          ui_feedback(UI_FB_NAV);
        }
        break;
      case INPUT_BTN_OK:
        if (press) {
          switch (capture_result_selected(&s_cr)) {
            case CAP_ACT_PRIMARY:
              send_captured();
              ui_feedback(UI_FB_EMULATE);
              notify(NOTIFY_INFO, "Signal sent");
              break;
            case CAP_ACT_SAVE:
              if (!s_saved) {
                if (save_captured()) {
                  s_saved = true;
                  capture_result_mark_saved(&s_cr);
                  ui_feedback(UI_FB_WRITE);
                  notify(NOTIFY_SAVED, "IR signal saved");
                } else {
                  notify(NOTIFY_WARNING, "Save failed");
                }
              }
              break;
            case CAP_ACT_AGAIN:
              start_capture();
              break;
            case CAP_ACT_DISCARD:
              stop_listening();
              ui_switch_screen(SCREEN_IR_MENU);
              return;
            default:
              break;
          }
        }
        break;
      default:
        break;
    }
  }
}

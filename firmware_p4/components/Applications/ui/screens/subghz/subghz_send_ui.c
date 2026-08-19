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

#include "subghz_send_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "capture_result_ui.h"
#include "notify_ui.h"
#include "subghz_scope_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

static const char *TAG = "SUBGHZ_SEND_UI";

#define ANTENNA_ICON "/assets/icons/settings_input_antenna.bin"
#define SIGNAL_ICON  "/assets/icons/graphic_eq.bin"

#define REFRESH_TICK_MS 50
#define SENDING_MS      1600
#define DOT_CYCLE_MS    350
#define REVEAL_MS       2600

#define STATUS_Y    48
#define FREQ_Y      68
#define SCOPE_Y_OFS -8

#define WAVES_ICON "/assets/icons/waves.bin"

#define LIST_PAD_SIDE   8
#define LIST_PAD_ROW    6
#define LIST_SB_W       4
#define LIST_SB_RADIUS  2
#define ROW_H           44
#define ROW_RADIUS      10
#define ROW_PAD_H       10
#define ROW_COL_GAP     10
#define ROW_GLYPH_SLOT  28
#define ROW_GLOW_W      14
#define ROW_GLOW_SPREAD -2

#define STATUS_SENDING "Transmitting"
#define STATUS_SENT    "Signal sent!"

#define HINT_SENDING "Transmitting..."
#define HINT_SENT    "BACK = Exit"
#define HINT_LIST    "UP/DOWN choose   OK send   BACK exit"
#define HINT_OPTIONS "UP/DOWN choose   OK do   BACK exit"

static const struct {
  const char *name;
  const char *freq;
  const char *proto;
} SIGNALS[] = {
    {"Gate_433", "433.92 MHz", "Princeton"},
    {"Doorbell", "433.92 MHz", "CAME"},
    {"TPMS_FL", "315.00 MHz", "TPMS"},
    {"Garage", "868.30 MHz", "Nice FLO"},
};
#define SIGNAL_COUNT ((int)(sizeof(SIGNALS) / sizeof(SIGNALS[0])))

typedef enum {
  VIEW_LIST = 0,
  VIEW_SENDING,
  VIEW_SENT,
} send_view_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_rows[SIGNAL_COUNT];
static lv_obj_t *s_row_name[SIGNAL_COUNT];
static lv_obj_t *s_row_val[SIGNAL_COUNT];
static send_view_t s_view = VIEW_LIST;
static int s_sel = 0;

static lv_timer_t *s_refresh_timer = NULL;
static lv_timer_t *s_send_timer = NULL;

static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_freq_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_sig = NULL;
static capture_result_t s_cr = {0};
static bool s_options = false;
static bool s_saved = false;
static uint32_t s_send_start = 0;
static uint32_t s_sent_at = 0;

static void subghz_send_input(const input_event_t *ev, void *ctx);
static void refresh_tick_cb(lv_timer_t *t);
static void build_list(void);
static void build_sending(void);
static void send_done_cb(lv_timer_t *t);

static void stop_send_timer(void) {
  if (s_send_timer != NULL) {
    lv_timer_delete(s_send_timer);
    s_send_timer = NULL;
  }
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

static void set_status(const char *text, bool success) {
  if (s_status_label == NULL)
    return;
  lv_label_set_text(s_status_label, text);
  lv_obj_set_style_text_color(
      s_status_label, success ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_main, 0);
}

static void set_hint(const char *text) {
  if (s_hint_label != NULL)
    ui_chrome_footer_set_text(s_hint_label, text);
}

static void style_row(int i, bool sel) {
  lv_obj_t *r = s_rows[i];
  if (r == NULL)
    return;
  if (sel) {
    lv_obj_set_style_bg_color(r, current_theme.bg_secondary, 0);
    lv_obj_set_style_border_color(r, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_color(r, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(r, ROW_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(r, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(r, ROW_GLOW_SPREAD, 0);
    lv_obj_set_style_text_color(s_row_name[i], current_theme.text_main, 0);
    lv_obj_set_style_text_color(s_row_val[i], current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_bg_color(r, current_theme.bg_primary, 0);
    lv_obj_set_style_border_color(r, current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(r, 0, 0);
    lv_obj_set_style_text_color(s_row_name[i], current_theme.text_secondary, 0);
    lv_obj_set_style_text_color(s_row_val[i], current_theme.text_secondary, 0);
  }
}

static void update_selection(void) {
  for (int i = 0; i < SIGNAL_COUNT; i++)
    style_row(i, i == s_sel);
  if (s_list != NULL && s_rows[s_sel] != NULL) {
    lv_obj_update_layout(s_list);
    lv_obj_scroll_to_view(s_rows[s_sel], LV_ANIM_ON);
  }
}

static void build_list(void) {
  subghz_scope_stop();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_label = NULL;
  s_freq_label = NULL;
  s_hint_label = NULL;
  s_sig = NULL;
  s_list = NULL;
  s_cr = (capture_result_t){0};
  s_options = false;

  if (s_sel < 0)
    s_sel = 0;
  if (s_sel >= SIGNAL_COUNT)
    s_sel = SIGNAL_COUNT - 1;

  s_screen = new_screen();
  ui_chrome_header(s_screen, "SEND", ANTENNA_ICON);

  lv_obj_t *cont = lv_obj_create(s_screen);
  s_list = cont;
  lv_obj_set_size(cont, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_left(cont, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_right(cont, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_row(cont, LIST_PAD_ROW, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_ON);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(cont, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(cont, LIST_SB_W, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(cont, LIST_SB_RADIUS, LV_PART_SCROLLBAR);

  for (int i = 0; i < SIGNAL_COUNT; i++) {
    lv_obj_t *r = lv_obj_create(cont);
    s_rows[i] = r;
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, ROW_H);
    lv_obj_set_style_radius(r, ROW_RADIUS, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(r, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_pad_hor(r, ROW_PAD_H, 0);
    lv_obj_set_style_pad_ver(r, 0, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, ROW_COL_GAP, 0);

    lv_obj_t *slot = lv_obj_create(r);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(slot, ROW_GLYPH_SLOT, ROW_GLYPH_SLOT);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slot, 0, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);

    lv_image_dsc_t *dsc = assets_get(WAVES_ICON);
    if (dsc != NULL) {
      lv_obj_t *img = lv_image_create(slot);
      lv_image_set_src(img, dsc);
      lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
      lv_obj_set_size(img, ROW_GLYPH_SLOT, ROW_GLYPH_SLOT);
      lv_obj_center(img);
      lv_obj_set_style_image_recolor(img, current_theme.border_accent, 0);
      lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    }

    lv_obj_t *name = lv_label_create(r);
    s_row_name[i] = name;
    lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_text(name, SIGNALS[i].name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);

    lv_obj_t *val = lv_label_create(r);
    s_row_val[i] = val;
    lv_label_set_text(val, SIGNALS[i].freq);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  }

  s_hint_label = ui_chrome_footer(s_screen, HINT_LIST);
  update_selection();

  ui_screen_load_owned(&s_screen, s_screen);
}

static void build_sending(void) {
  subghz_scope_stop();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_list = NULL;
  s_cr = (capture_result_t){0};
  s_options = false;

  s_screen = new_screen();
  ui_chrome_header(s_screen, "SEND", ANTENNA_ICON);

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, STATUS_SENDING);
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_freq_label = lv_label_create(s_screen);
  lv_label_set_text(s_freq_label, SIGNALS[s_sel].freq);
  lv_obj_set_style_text_color(s_freq_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_freq_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_freq_label, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  s_sig = subghz_scope_create(s_screen, LV_ALIGN_CENTER, 0, SCOPE_Y_OFS);

  s_hint_label = ui_chrome_footer(s_screen, HINT_SENDING);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void show_options(void) {
  subghz_scope_stop();
  if (s_sig != NULL) {
    lv_obj_del(s_sig);
    s_sig = NULL;
  }
  if (s_status_label != NULL)
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
  if (s_freq_label != NULL)
    lv_obj_add_flag(s_freq_label, LV_OBJ_FLAG_HIDDEN);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = SIGNAL_ICON,
      .card_title = SIGNALS[s_sel].name,
      .card_sub = SIGNALS[s_sel].proto,
      .card_value = SIGNALS[s_sel].freq,
      .primary_label = "Send again",
      .again_label = "Pick another",
  };
  s_cr = capture_result_create(s_screen, &cfg);
  s_options = true;
  s_saved = false;
  set_hint(HINT_OPTIONS);
}

static void start_send(void) {
  stop_send_timer();
  s_view = VIEW_SENDING;
  s_send_start = lv_tick_get();
  ESP_LOGI(TAG, "mock send: %s %s", SIGNALS[s_sel].name, SIGNALS[s_sel].freq);
  build_sending();
  ui_feedback(UI_FB_SELECT);
  s_send_timer = lv_timer_create(send_done_cb, SENDING_MS, NULL);
  lv_timer_set_repeat_count(s_send_timer, 1);
}

static void send_done_cb(lv_timer_t *t) {
  (void)t;
  s_send_timer = NULL;
  if (lv_screen_active() != s_screen)
    return;

  s_view = VIEW_SENT;
  s_options = false;
  set_status(STATUS_SENT, true);

  subghz_scope_lock();

  s_sent_at = lv_tick_get();
  set_hint(HINT_SENT);
  ui_feedback(UI_FB_WRITE);
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

static void refresh_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_refresh_timer = NULL;
    return;
  }

  if (s_view == VIEW_SENDING) {
    sending_tick();
  } else if (s_view == VIEW_SENT && !s_options) {
    if (lv_tick_get() - s_sent_at >= REVEAL_MS)
      show_options();
  }
}

static void subghz_send_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_view) {
    case VIEW_LIST:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_sel < SIGNAL_COUNT - 1) {
            s_sel++;
            update_selection();
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_UP:
          if (nav && s_sel > 0) {
            s_sel--;
            update_selection();
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_OK:
        case INPUT_BTN_RIGHT:
          if (press)
            start_send();
          break;
        case INPUT_BTN_BACK:
        case INPUT_BTN_LEFT:
          if (press)
            ui_switch_screen(SCREEN_SUBGHZ_MENU);
          break;
        default:
          break;
      }
      break;

    case VIEW_SENDING:
      switch (ev->button) {
        case INPUT_BTN_BACK:
          if (press) {
            stop_send_timer();
            s_view = VIEW_LIST;
            build_list();
          }
          break;
        default:
          break;
      }
      break;

    case VIEW_SENT:
      if (!s_options) {
        switch (ev->button) {
          case INPUT_BTN_BACK:
            if (press) {
              s_view = VIEW_LIST;
              build_list();
            }
            break;
          default:
            break;
        }
      } else {
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
                  start_send();
                  break;
                case CAP_ACT_SAVE:
                  if (!s_saved) {
                    s_saved = true;
                    capture_result_mark_saved(&s_cr);
                    ESP_LOGI(TAG, "mock signal saved: %s", SIGNALS[s_sel].name);
                    ui_feedback(UI_FB_WRITE);
                    notify(NOTIFY_SAVED, "Sub-GHz signal saved");
                  }
                  break;
                case CAP_ACT_AGAIN:
                  s_view = VIEW_LIST;
                  build_list();
                  break;
                case CAP_ACT_DISCARD:
                  ui_switch_screen(SCREEN_SUBGHZ_MENU);
                  break;
                default:
                  break;
              }
            }
            break;
          case INPUT_BTN_BACK:
            if (press)
              ui_switch_screen(SCREEN_SUBGHZ_MENU);
            break;
          default:
            break;
        }
      }
      break;

    default:
      break;
  }
}

void ui_subghz_send_open(void) {
  stop_send_timer();
  subghz_scope_stop();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_label = NULL;
  s_freq_label = NULL;
  s_hint_label = NULL;
  s_sig = NULL;
  s_list = NULL;
  s_cr = (capture_result_t){0};
  s_options = false;
  s_saved = false;
  s_view = VIEW_LIST;
  s_sel = 0;

  build_list();

  if (s_refresh_timer == NULL)
    s_refresh_timer = lv_timer_create(refresh_tick_cb, REFRESH_TICK_MS, NULL);

  ui_input_set_screen_handler(subghz_send_input, NULL);
}

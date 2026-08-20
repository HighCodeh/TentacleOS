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

#include "esp_attr.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "assets_manager.h"
#include "capture_result_ui.h"
#include "ir_store.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "sigwave_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"

static const char *TAG = "IR_SEND_UI";

#define IR_ICON "/assets/icons/podcasts.bin"

#define TICK_MS      50
#define SENDING_MS   1600
#define DOT_CYCLE_MS 350
#define REVEAL_MS    2600

#define STATUS_Y   50
#define CARD_W     210
#define CARD_H     64
#define CARD_Y_OFS -30
#define SIG_Y_OFS  44

#define STATUS_SENDING "Sending"
#define STATUS_SENT    "Signal sent!"

#define HINT_SENDING "Transmitting..."
#define HINT_SENT    "BACK = Exit"
#define HINT_OPTIONS "UP/DOWN choose   OK do   BACK exit"

#define EMPTY_NAME "No signals - use Learn"

typedef enum {
  VIEW_LIST = 0,
  VIEW_SENDING,
  VIEW_SENT,
} send_view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static send_view_t s_view = VIEW_LIST;
static int s_sel = 0;

EXT_RAM_BSS_ATTR static ir_store_entry_t s_entries[IR_STORE_MAX_ENTRIES];
static int s_count = 0;

static lv_timer_t *s_tick_timer = NULL;
static lv_timer_t *s_send_timer = NULL;

static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_card = NULL;
static lv_obj_t *s_sig = NULL;
static capture_result_t s_cr = {0};
static bool s_options = false;
static uint32_t s_send_start = 0;
static uint32_t s_sent_at = 0;

static void ir_send_tick_cb(lv_timer_t *t);
static void ir_send_input(const input_event_t *ev, void *ctx);
static void build_list(void);
static void build_sending(void);
static void send_done_cb(lv_timer_t *t);

static const char *sel_name(void) {
  return (s_count > 0 && s_sel >= 0 && s_sel < s_count) ? s_entries[s_sel].name : EMPTY_NAME;
}

static const char *sel_proto(void) {
  return (s_count > 0 && s_sel >= 0 && s_sel < s_count) ? s_entries[s_sel].proto : "IR";
}

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

static void transy_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void card_rise(lv_obj_t *o) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, transy_cb);
  lv_anim_set_values(&a, 26, 0);
  lv_anim_set_duration(&a, 300);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, 13, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, 16, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  return p;
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

static lv_obj_t *build_cartridge(lv_obj_t *parent) {
  lv_obj_t *card = lit_panel(parent, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, 10, 0);

  lv_obj_t *well = lv_obj_create(card);
  lv_obj_remove_flag(well, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(well, 40, 40);
  lv_obj_set_style_radius(well, 10, 0);
  lv_obj_set_style_bg_color(well, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(well, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(well, 0, 0);
  lv_obj_set_style_pad_all(well, 0, 0);

  lv_image_dsc_t *dsc = assets_get(IR_ICON);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(well);
    lv_image_set_src(img, dsc);
    lv_obj_center(img);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  }

  lv_obj_t *col = lv_obj_create(card);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 3, 0);

  lv_obj_t *name = lv_label_create(col);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, lv_pct(100));
  lv_label_set_text(name, sel_name());
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);

  lv_obj_t *sub = lv_label_create(col);
  lv_label_set_text_fmt(sub, "Transmitting %s", sel_proto());
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sub, current_theme.border_accent, 0);

  return card;
}

static void build_list(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_label = NULL;
  s_hint_label = NULL;
  s_card = NULL;
  s_sig = NULL;
  s_cr = (capture_result_t){0};
  s_options = false;

  s_count = ir_store_list(s_entries, IR_STORE_MAX_ENTRIES);
  if (s_count < 0)
    s_count = 0;
  if (s_count > MENU_COMP_MAX_ITEMS) {
    // menu_component holds at most MENU_COMP_MAX_ITEMS rows — don't let extra
    // files silently fall off the end (selection math would desync). Browse/Burst
    // still reach the rest.
    ESP_LOGW(TAG, "%d IR files; showing first %d", s_count, MENU_COMP_MAX_ITEMS);
    s_count = MENU_COMP_MAX_ITEMS;
  }
  if (s_sel >= s_count)
    s_sel = (s_count > 0) ? s_count - 1 : 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "IR SEND", IR_ICON);
  if (s_count == 0) {
    menu_component_add_item(&s_menu, "/assets/icons/graphic_eq.bin", EMPTY_NAME);
  } else {
    for (int i = 0; i < s_count; i++)
      menu_component_add_item(&s_menu, "/assets/icons/graphic_eq.bin", s_entries[i].name);
    if (s_sel > 0 && s_sel < s_count)
      menu_component_select(&s_menu, s_sel);
  }

  ui_screen_load_owned(&s_screen, s_screen);
}

static void build_sending(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_cr = (capture_result_t){0};
  s_options = false;

  s_screen = new_screen();
  ui_chrome_header(s_screen, "Send", IR_ICON);

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, STATUS_SENDING);
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_card = build_cartridge(s_screen);
  s_sig = sigwave_create(s_screen, LV_ALIGN_CENTER, 0, SIG_Y_OFS);

  s_hint_label = ui_chrome_footer(s_screen, HINT_SENDING);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void show_options(void) {
  if (s_card != NULL) {
    lv_obj_del(s_card);
    s_card = NULL;
  }
  if (s_sig != NULL) {
    lv_obj_del(s_sig);
    s_sig = NULL;
  }
  if (s_status_label != NULL) {
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
  }

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = IR_ICON,
      .card_title = sel_name(),
      .card_sub = sel_proto(),
      .card_value = "sent",
      .primary_label = "Send again",
      .again_label = "Pick another",
  };
  s_cr = capture_result_create(s_screen, &cfg);
  s_options = true;
  set_hint(HINT_OPTIONS);
}

// Actually transmit the selected file's first signal. Quick and blocking; the
// on-screen "Sending" animation continues cosmetically.
static void transmit_selected(void) {
  if (s_count <= 0 || s_sel < 0 || s_sel >= s_count)
    return;
  ir_file_t f;
  ir_file_init(&f);
  esp_err_t r = ESP_FAIL;
  if (ir_store_load(s_entries[s_sel].path, &f) == ESP_OK && f.count > 0)
    r = ir_store_send_signal(&f.signals[0]);
  else
    ESP_LOGW(TAG, "load %s failed", s_entries[s_sel].path);
  ir_file_free(&f);
  if (r != ESP_OK)
    notify(NOTIFY_WARNING, "Send failed");
}

static void start_send(void) {
  stop_send_timer();
  s_view = VIEW_SENDING;
  s_send_start = lv_tick_get();
  transmit_selected();
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

  if (s_sig != NULL) {
    lv_obj_del(s_sig);
    s_sig = NULL;
  }
  s_sig = sigwave_create_static(s_screen, LV_ALIGN_CENTER, 0, SIG_Y_OFS);

  if (s_card != NULL) {
    lv_obj_fade_in(s_card, 280, 0);
    card_rise(s_card);
  }

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

static void ir_send_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_tick_timer = NULL;
    return;
  }

  if (s_view == VIEW_SENDING) {
    sending_tick();
  } else if (s_view == VIEW_SENT && !s_options && lv_tick_get() - s_sent_at >= REVEAL_MS) {
    show_options();
  }
}

static void ir_send_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_view) {
    case VIEW_LIST:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav) {
            menu_component_next(&s_menu);
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_UP:
          if (nav) {
            menu_component_prev(&s_menu);
            ui_feedback(UI_FB_NAV);
          }
          break;
        case INPUT_BTN_OK:
        case INPUT_BTN_RIGHT:
          if (press) {
            if (s_count <= 0) {
              notify(NOTIFY_INFO, "Capture a signal in Learn first");
            } else {
              s_sel = menu_component_get_selected(&s_menu);
              start_send();
            }
          }
          break;
        case INPUT_BTN_BACK:
        case INPUT_BTN_LEFT:
          if (press)
            ui_switch_screen(SCREEN_IR_MENU);
          break;
        default:
          break;
      }
      break;

    case VIEW_SENDING:
      if (ev->button == INPUT_BTN_BACK && press) {
        stop_send_timer();
        s_view = VIEW_LIST;
        build_list();
      }
      break;

    case VIEW_SENT:
      if (!s_options) {
        if (ev->button == INPUT_BTN_BACK && press) {
          s_view = VIEW_LIST;
          build_list();
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
                  capture_result_mark_saved(&s_cr);
                  notify(NOTIFY_INFO, "Already saved");
                  break;
                case CAP_ACT_AGAIN:
                  s_view = VIEW_LIST;
                  build_list();
                  break;
                case CAP_ACT_DISCARD:
                  ui_switch_screen(SCREEN_IR_MENU);
                  break;
                default:
                  break;
              }
            }
            break;
          case INPUT_BTN_BACK:
            if (press)
              ui_switch_screen(SCREEN_IR_MENU);
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

void ui_ir_send_open(void) {
  stop_send_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_label = NULL;
  s_hint_label = NULL;
  s_card = NULL;
  s_sig = NULL;
  s_cr = (capture_result_t){0};
  s_options = false;
  s_view = VIEW_LIST;
  s_sel = 0;

  build_list();

  ui_input_set_screen_handler(ir_send_input, NULL);
  if (s_tick_timer == NULL)
    s_tick_timer = lv_timer_create(ir_send_tick_cb, TICK_MS, NULL);
}

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

#include "ir_burst_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "ir_store.h"
#include "notify_ui.h"
#include "sigwave_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_BURST_UI";

#define SIG_GREEN 0x00E676
#define COL_DIM   0x8A8594
#define BAR_TRACK 0x202028

#define IR_BURST_ICON "/assets/icons/bolt.bin"

#define STATUS_Y 48
#define COUNT_Y  72
#define BAR_Y    94
#define BAR_W    200
#define BAR_H    8

#define CARD_W       200
#define CARD_H       86
#define CARD_RADIUS  13
#define CARD_Y_OFS   16
#define CARD_RISE_PX 26
#define CARD_RISE_MS 300

#define LOG_Y -30

#define BURST_TICK_MS 180

#define STATUS_BUSY  "Bursting..."
#define STATUS_DONE  "Burst complete!"
#define STATUS_EMPTY "No saved signals"
#define HINT_BUSY    "BACK to cancel"
#define HINT_DONE    "BACK = Exit"

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_burst_timer = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_card = NULL;
static lv_obj_t *s_caption = NULL;
static lv_obj_t *s_sig = NULL;
static lv_obj_t *s_log_label = NULL;
static lv_obj_t *s_footer = NULL;
static int s_sent = 0;

static ir_store_entry_t s_entries[IR_STORE_MAX_ENTRIES];
static int s_total = 0;

static void ir_burst_input(const input_event_t *ev, void *ctx);
static void burst_tick_cb(lv_timer_t *timer);

static void transy_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, 16, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  lv_obj_set_style_pad_all(p, 8, 0);
  return p;
}

static void card_rise(lv_obj_t *obj) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, transy_cb);
  lv_anim_set_values(&a, CARD_RISE_PX, 0);
  lv_anim_set_duration(&a, CARD_RISE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  lv_anim_t f;
  lv_anim_init(&f);
  lv_anim_set_var(&f, obj);
  lv_anim_set_exec_cb(&f, opa_cb);
  lv_anim_set_values(&f, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&f, 280);
  lv_anim_set_path_cb(&f, lv_anim_path_ease_out);
  lv_anim_start(&f);
}

void ui_ir_burst_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sent = 0;
  s_burst_timer = NULL;
  s_card = NULL;
  s_sig = NULL;
  s_caption = NULL;

  s_total = ir_store_list(s_entries, IR_STORE_MAX_ENTRIES);
  if (s_total < 0)
    s_total = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "IR BURST", IR_BURST_ICON);

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, s_total > 0 ? STATUS_BUSY : STATUS_EMPTY);
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_count_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_count_label, "0 / %d files", s_total);
  lv_obj_set_style_text_color(s_count_label, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_count_label, LV_ALIGN_TOP_MID, 0, COUNT_Y);

  s_bar = lv_bar_create(s_screen);
  lv_obj_set_size(s_bar, BAR_W, BAR_H);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, BAR_Y);
  lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(BAR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(s_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(s_bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(s_bar, 0, s_total > 0 ? s_total : 1);
  lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

  s_card = lit_panel(s_screen, CARD_W, CARD_H);
  lv_obj_align(s_card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);

  s_caption = lv_label_create(s_card);
  lv_label_set_text(s_caption, "IR emitter  38 kHz");
  lv_obj_set_style_text_color(s_caption, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_caption, &lv_font_montserrat_12, 0);
  lv_obj_align(s_caption, LV_ALIGN_TOP_MID, 0, 0);

  s_sig = sigwave_create(s_card, LV_ALIGN_BOTTOM_MID, 0, -2);

  s_log_label = lv_label_create(s_screen);
  lv_label_set_text(s_log_label, s_total > 0 ? "" : "Capture signals in Learn first");
  lv_obj_set_style_text_color(s_log_label, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(s_log_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_log_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_log_label, LV_ALIGN_BOTTOM_MID, 0, LOG_Y);

  s_footer = ui_chrome_footer(s_screen, s_total > 0 ? HINT_BUSY : HINT_DONE);

  ui_input_set_screen_handler(ir_burst_input, NULL);

  if (s_total > 0)
    s_burst_timer = lv_timer_create(burst_tick_cb, BURST_TICK_MS, NULL);
  else
    s_burst_timer = NULL;

  ui_screen_load(s_screen);
}

// Transmit the first signal of file s_entries[idx]; returns true on success.
static bool burst_send_one(int idx) {
  if (idx < 0 || idx >= s_total)
    return false;
  ir_file_t f;
  ir_file_init(&f);
  bool ok = false;
  if (ir_store_load(s_entries[idx].path, &f) == ESP_OK && f.count > 0)
    ok = (ir_store_send_signal(&f.signals[0]) == ESP_OK);
  ir_file_free(&f);
  return ok;
}

static void burst_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_burst_timer == timer)
      s_burst_timer = NULL;
    return;
  }

  bool ok = burst_send_one(s_sent);
  s_sent++;
  lv_label_set_text_fmt(s_count_label, "%d / %d files", s_sent, s_total);
  if (s_bar)
    lv_bar_set_value(s_bar, s_sent, LV_ANIM_OFF);
  if (s_log_label)
    lv_label_set_text_fmt(s_log_label, "> %-16s %s", s_entries[s_sent - 1].name, ok ? "OK" : "--");

  if (s_sent >= s_total) {
    lv_label_set_text(s_status_label, STATUS_DONE);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(SIG_GREEN), 0);
    lv_label_set_text_fmt(s_count_label, "%d / %d files", s_total, s_total);
    if (s_footer)
      ui_chrome_footer_set_text(s_footer, HINT_DONE);
    if (s_log_label)
      lv_obj_add_flag(s_log_label, LV_OBJ_FLAG_HIDDEN);

    if (s_sig) {
      lv_obj_del(s_sig);
      s_sig = sigwave_create_static(s_card, LV_ALIGN_BOTTOM_MID, 0, -2);
    }
    if (s_caption) {
      lv_label_set_text_fmt(s_caption, "%d signals sent", s_total);
      lv_obj_set_style_text_color(s_caption, current_theme.text_main, 0);
      lv_obj_set_style_text_font(s_caption, &lv_font_montserrat_14, 0);
    }
    if (s_card)
      card_rise(s_card);

    ESP_LOGI(TAG, "burst complete: %d signals", s_total);
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_SAVED, "Burst sent");

    lv_timer_delete(timer);
    s_burst_timer = NULL;
  }
}

static void ir_burst_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        if (s_burst_timer != NULL) {
          lv_timer_delete(s_burst_timer);
          s_burst_timer = NULL;
        }
        ESP_LOGI(TAG, "burst cancelled");
        ui_switch_screen(SCREEN_IR_MENU);
      }
      break;
    default:
      break;
  }
}

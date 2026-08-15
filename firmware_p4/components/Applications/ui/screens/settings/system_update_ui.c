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

#include "system_update_ui.h"

#include <stdio.h>

#include "lvgl.h"

#include "reboot_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

#define SEARCH_MS       2600
#define APPLY_TICK_MS   45
#define APPLY_STEP      2
#define PROG_MAX        100

#define CONTENT_Y_OFS ((UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2)

#define BLINK_MS 620
#define GLOW_MS  900
#define GLOW_MIN 16
#define GLOW_MAX 34

#define DOT_COUNT      3
#define DOT_SIZE       8
#define DOT_GAP        16
#define DOT_PULSE_MS   480
#define DOT_STAGGER_MS 150

#define SCAN_W    176
#define SCAN_H    8
#define SCAN_HI_W 56
#define SCAN_MS   1050
#define SCAN_RAD  4

#define SEARCH_WAVES_Y -46
#define SEARCH_LBL_Y   48
#define SEARCH_DOTS_Y  78
#define SEARCH_SCAN_Y  108

#define FOUND_CARD_W 208
#define FOUND_CARD_H 160
#define APPLY_CARD_H 120
#define CARD_RADIUS  14
#define CARD_PAD     14

#define PILL_W   96
#define PILL_H   30
#define PILL_RAD 10

#define BAR_W   176
#define BAR_H   14
#define BAR_RAD 5

#define STEP_LBL_Y  -22
#define APPLY_BAR_Y 18

#define PH_VERIFY_MAX 18
#define PH_ERASE_MAX  42
#define PH_WRITE_MAX  96

#define COL_SUCCESS 0x00E676
#define COL_DIM     0x8A8594
#define COL_TRACK   0x202028

#define NEW_VERSION       "v2.1.0"
#define INSTALLED_VERSION "v2.0.0"

#define HDR_ICON  "/assets/icons/system_update.bin"
#define HDR_TITLE "P4 UPDATE"

#define FOOTER_SEARCH "BACK Cancel"
#define FOOTER_FOUND  "OK Update    BACK Cancel"
#define FOOTER_APPLY  "Updating - do not power off"

#define SEARCH_TEXT   "Searching for updates..."
#define CARD_TITLE    LV_SYMBOL_DOWNLOAD "  Update available"
#define CAPTION_NEW   "New version"
#define INSTALLED_ROW "Installed  " INSTALLED_VERSION

#define STEP_VERIFY    "Verifying image"
#define STEP_ERASE     "Erasing"
#define STEP_FINALIZE  "Finalizing"
#define STEP_WRITE_FMT "Writing %d%%"
#define STEP_BUF_LEN   24

typedef enum {
  ST_SEARCHING = 0,
  ST_FOUND,
  ST_APPLYING,
} update_state_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_step_lbl = NULL;
static lv_timer_t *s_phase_timer = NULL;
static lv_timer_t *s_apply_timer = NULL;

static update_state_t s_state = ST_SEARCHING;
static int s_pct = 0;

static void system_update_input(const input_event_t *ev, void *ctx);
static void build_screen(void);

static void stop_phase_timer(void) {
  if (s_phase_timer != NULL) {
    lv_timer_delete(s_phase_timer);
    s_phase_timer = NULL;
  }
}

static void stop_apply_timer(void) {
  if (s_apply_timer != NULL) {
    lv_timer_delete(s_apply_timer);
    s_apply_timer = NULL;
  }
}

static void opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void translate_x_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void glow_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_shadow_width((lv_obj_t *)var, v, 0);
}

static void start_blink(lv_obj_t *o, uint32_t ms, lv_opa_t lo, lv_opa_t hi) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, opa_anim_cb);
  lv_anim_set_values(&a, lo, hi);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_playback_duration(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static lv_obj_t *make_lit_card(int h) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, FOUND_CARD_W, h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CONTENT_Y_OFS);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, CARD_PAD, 0);
  lv_obj_set_style_shadow_width(card, GLOW_MIN, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  return card;
}

static void build_searching(void) {
  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);
  ui_chrome_footer(s_screen, FOOTER_SEARCH);

  waves_create(s_screen, LV_ALIGN_CENTER, 0, SEARCH_WAVES_Y, LV_SYMBOL_DOWNLOAD, NULL);

  lv_obj_t *lbl = lv_label_create(s_screen);
  lv_label_set_text(lbl, SEARCH_TEXT);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, current_theme.border_accent, 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, SEARCH_LBL_Y);
  start_blink(lbl, BLINK_MS, LV_OPA_40, LV_OPA_COVER);

  lv_obj_t *dots = lv_obj_create(s_screen);
  lv_obj_remove_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dots, DOT_COUNT * DOT_SIZE + (DOT_COUNT - 1) * DOT_GAP, DOT_SIZE);
  lv_obj_align(dots, LV_ALIGN_CENTER, 0, SEARCH_DOTS_Y);
  lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dots, 0, 0);
  lv_obj_set_style_pad_all(dots, 0, 0);

  int x0 = -(DOT_COUNT * DOT_SIZE + (DOT_COUNT - 1) * DOT_GAP) / 2 + DOT_SIZE / 2;
  for (int i = 0; i < DOT_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(dots);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
    lv_obj_align(dot, LV_ALIGN_CENTER, x0 + i * (DOT_SIZE + DOT_GAP), 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, current_theme.border_accent, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&a, DOT_PULSE_MS);
    lv_anim_set_playback_duration(&a, DOT_PULSE_MS);
    lv_anim_set_delay(&a, i * DOT_STAGGER_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }

  lv_obj_t *track = lv_obj_create(s_screen);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(track, SCAN_W, SCAN_H);
  lv_obj_align(track, LV_ALIGN_CENTER, 0, SEARCH_SCAN_Y);
  lv_obj_set_style_radius(track, SCAN_RAD, 0);
  lv_obj_set_style_border_width(track, 0, 0);
  lv_obj_set_style_pad_all(track, 0, 0);
  lv_obj_set_style_bg_color(track, lv_color_hex(COL_TRACK), 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_clip_corner(track, true, 0);

  lv_obj_t *hi = lv_obj_create(track);
  lv_obj_remove_flag(hi, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hi, SCAN_HI_W, SCAN_H);
  lv_obj_align(hi, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(hi, SCAN_RAD, 0);
  lv_obj_set_style_border_width(hi, 0, 0);
  lv_obj_set_style_bg_color(hi, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(hi, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(hi, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(hi, 10, 0);
  lv_obj_set_style_shadow_opa(hi, LV_OPA_50, 0);

  lv_anim_t sa;
  lv_anim_init(&sa);
  lv_anim_set_var(&sa, hi);
  lv_anim_set_exec_cb(&sa, translate_x_cb);
  lv_anim_set_values(&sa, 0, SCAN_W - SCAN_HI_W);
  lv_anim_set_duration(&sa, SCAN_MS);
  lv_anim_set_playback_duration(&sa, SCAN_MS);
  lv_anim_set_repeat_count(&sa, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&sa, lv_anim_path_ease_in_out);
  lv_anim_start(&sa);
}

static void phase_advance_cb(lv_timer_t *t) {
  (void)t;
  s_phase_timer = NULL;
  if (lv_screen_active() != s_screen || s_state != ST_SEARCHING)
    return;
  s_state = ST_FOUND;
  build_screen();
}

static void build_found(void) {
  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);
  ui_chrome_footer(s_screen, FOOTER_FOUND);

  lv_obj_t *card = make_lit_card(FOUND_CARD_H);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, CARD_TITLE);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *cap = lv_label_create(card);
  lv_label_set_text(cap, CAPTION_NEW);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_DIM), 0);
  lv_obj_align(cap, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t *pill = lv_obj_create(card);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pill, PILL_W, PILL_H);
  lv_obj_align(pill, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_style_radius(pill, PILL_RAD, 0);
  lv_obj_set_style_pad_all(pill, 0, 0);
  lv_obj_set_style_bg_color(pill, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
  lv_obj_set_style_border_color(pill, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(pill, 1, 0);

  lv_obj_t *ver = lv_label_create(pill);
  lv_label_set_text(ver, NEW_VERSION);
  lv_obj_set_style_text_font(ver, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ver, current_theme.border_accent, 0);
  lv_obj_center(ver);

  lv_obj_t *inst = lv_label_create(card);
  lv_label_set_text(inst, INSTALLED_ROW);
  lv_obj_set_style_text_font(inst, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(inst, lv_color_hex(COL_DIM), 0);
  lv_obj_align(inst, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void set_step_label(void) {
  if (s_step_lbl == NULL)
    return;
  if (s_pct < PH_VERIFY_MAX) {
    lv_label_set_text(s_step_lbl, STEP_VERIFY);
    lv_obj_set_style_text_color(s_step_lbl, current_theme.border_accent, 0);
  } else if (s_pct < PH_ERASE_MAX) {
    lv_label_set_text(s_step_lbl, STEP_ERASE);
    lv_obj_set_style_text_color(s_step_lbl, current_theme.border_accent, 0);
  } else if (s_pct < PH_WRITE_MAX) {
    char buf[STEP_BUF_LEN];
    snprintf(buf, sizeof(buf), STEP_WRITE_FMT, s_pct);
    lv_label_set_text(s_step_lbl, buf);
    lv_obj_set_style_text_color(s_step_lbl, lv_color_hex(COL_SUCCESS), 0);
  } else {
    lv_label_set_text(s_step_lbl, STEP_FINALIZE);
    lv_obj_set_style_text_color(s_step_lbl, lv_color_hex(COL_SUCCESS), 0);
  }
}

static void apply_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_apply_timer = NULL;
    return;
  }

  s_pct += APPLY_STEP;
  if (s_pct >= PROG_MAX)
    s_pct = PROG_MAX;

  if (s_bar)
    lv_bar_set_value(s_bar, s_pct, LV_ANIM_OFF);
  set_step_label();

  if (s_pct >= PROG_MAX) {
    lv_timer_delete(t);
    s_apply_timer = NULL;
    reboot_ui_reboot();
  }
}

static void build_applying(void) {
  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);
  ui_chrome_footer(s_screen, FOOTER_APPLY);

  lv_obj_t *card = make_lit_card(APPLY_CARD_H);

  lv_anim_t ga;
  lv_anim_init(&ga);
  lv_anim_set_var(&ga, card);
  lv_anim_set_exec_cb(&ga, glow_anim_cb);
  lv_anim_set_values(&ga, GLOW_MIN, GLOW_MAX);
  lv_anim_set_duration(&ga, GLOW_MS);
  lv_anim_set_playback_duration(&ga, GLOW_MS);
  lv_anim_set_repeat_count(&ga, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&ga, lv_anim_path_ease_in_out);
  lv_anim_start(&ga);

  s_step_lbl = lv_label_create(card);
  lv_obj_set_style_text_font(s_step_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_step_lbl, current_theme.border_accent, 0);
  lv_obj_set_style_text_align(s_step_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_step_lbl, LV_ALIGN_CENTER, 0, STEP_LBL_Y);

  s_bar = lv_bar_create(card);
  lv_obj_set_size(s_bar, BAR_W, BAR_H);
  lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, APPLY_BAR_Y);
  lv_bar_set_range(s_bar, 0, PROG_MAX);
  lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_bar, BAR_RAD, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(s_bar, lv_color_hex(COL_SUCCESS), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(s_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_bar, BAR_RAD, LV_PART_INDICATOR);

  s_pct = 0;
  set_step_label();
  ui_feedback(UI_FB_WRITE);

  s_apply_timer = lv_timer_create(apply_tick_cb, APPLY_TICK_MS, NULL);
}

static void build_screen(void) {
  stop_phase_timer();
  stop_apply_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_bar = NULL;
  s_step_lbl = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_state) {
    case ST_FOUND:
      build_found();
      break;
    case ST_APPLYING:
      build_applying();
      break;
    case ST_SEARCHING:
    default:
      build_searching();
      s_phase_timer = lv_timer_create(phase_advance_cb, SEARCH_MS, NULL);
      lv_timer_set_repeat_count(s_phase_timer, 1);
      break;
  }

  ui_input_set_screen_handler(system_update_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void system_update_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS)
    return;
  if (s_state == ST_APPLYING)
    return;

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      ui_switch_screen(SCREEN_DEV_MENU);
      break;
    case INPUT_BTN_OK:
      if (s_state == ST_FOUND) {
        ui_feedback(UI_FB_SELECT);
        s_state = ST_APPLYING;
        build_screen();
      }
      break;
    default:
      break;
  }
}

void ui_system_update_open(void) {
  s_phase_timer = NULL;
  s_apply_timer = NULL;
  s_bar = NULL;
  s_step_lbl = NULL;
  s_state = ST_SEARCHING;
  s_pct = 0;

  build_screen();
}

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

#include "notify_ui.h"

#include "lvgl.h"

#include "ui_feedback.h"
#include "ui_theme.h"

#define NOTIFY_MS  3200 // how long the pill stays before auto-dismissing
#define SLIDE_MS   220
#define TOP_Y      6 // resting offset from the very top (top layer)
#define PILL_MAX_W 224
#define TEXT_MAX_W 170
#define COL_RAISE  0x170A28

// One pill at a time; a new notify() replaces whatever is showing.
static lv_obj_t *s_pill = NULL;
static lv_timer_t *s_timer = NULL;

static lv_color_t type_color(notify_type_t t) {
  switch (t) {
    case NOTIFY_SAVED:
      return lv_color_hex(0x00E676);
    case NOTIFY_UPDATE:
      return lv_color_hex(0x00E676);
    case NOTIFY_LORA:
      return lv_color_hex(0x00BCD4);
    case NOTIFY_WARNING:
      return lv_color_hex(0xFFC400);
    default:
      return current_theme.border_accent; // NOTIFY_INFO
  }
}

static const char *type_sym(notify_type_t t) {
  switch (t) {
    case NOTIFY_SAVED:
      return LV_SYMBOL_OK; // the check mark
    case NOTIFY_UPDATE:
      return LV_SYMBOL_DOWNLOAD;
    case NOTIFY_LORA:
      return LV_SYMBOL_ENVELOPE;
    case NOTIFY_WARNING:
      return LV_SYMBOL_WARNING;
    default:
      return LV_SYMBOL_BELL;
  }
}

static void slide_y_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}
static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void gone_cb(lv_anim_t *a) {
  (void)a;
  // Only fires for a natural dismiss of the current pill (a replaced pill has
  // its animations deleted before this could run).
  if (s_pill != NULL) {
    lv_obj_del(s_pill);
    s_pill = NULL;
  }
}

static void clear_now(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_pill != NULL) {
    lv_anim_delete(s_pill, NULL); // drop any in-flight slide/fade (no gone_cb)
    lv_obj_del(s_pill);
    s_pill = NULL;
  }
}

static void dismiss_cb(lv_timer_t *t) {
  (void)t;
  s_timer = NULL;
  if (s_pill == NULL)
    return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_pill);
  lv_anim_set_exec_cb(&a, slide_y_cb);
  lv_anim_set_values(&a, 0, -50);
  lv_anim_set_duration(&a, SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&a, gone_cb);
  lv_anim_start(&a);

  lv_anim_t f;
  lv_anim_init(&f);
  lv_anim_set_var(&f, s_pill);
  lv_anim_set_exec_cb(&f, opa_cb);
  lv_anim_set_values(&f, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&f, SLIDE_MS);
  lv_anim_start(&f);
}

void notify(notify_type_t type, const char *text) {
  clear_now(); // replace any current pill

  lv_color_t c = type_color(type);
  lv_obj_t *pill = lv_obj_create(lv_layer_top());
  s_pill = pill;
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE); // never steals input
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(pill, PILL_MAX_W, 0);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(pill, lv_color_hex(COL_RAISE), 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pill, 1, 0);
  lv_obj_set_style_border_color(pill, c, 0);
  lv_obj_set_style_shadow_width(pill, 18, 0);
  lv_obj_set_style_shadow_color(pill, c, 0);
  lv_obj_set_style_shadow_spread(pill, -6, 0);
  lv_obj_set_style_shadow_ofs_y(pill, 6, 0);
  lv_obj_set_style_pad_hor(pill, 13, 0);
  lv_obj_set_style_pad_ver(pill, 7, 0);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 8, 0);
  lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, TOP_Y);

  lv_obj_t *icon = lv_label_create(pill);
  lv_label_set_text(icon, type_sym(type));
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(icon, c, 0);

  lv_obj_t *lbl = lv_label_create(pill);
  lv_label_set_text(lbl, text ? text : "");
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(lbl, TEXT_MAX_W, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);

  // Slide down + fade in.
  lv_obj_set_style_opa(pill, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, pill);
  lv_anim_set_exec_cb(&a, slide_y_cb);
  lv_anim_set_values(&a, -50, 0);
  lv_anim_set_duration(&a, SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  lv_anim_t f;
  lv_anim_init(&f);
  lv_anim_set_var(&f, pill);
  lv_anim_set_exec_cb(&f, opa_cb);
  lv_anim_set_values(&f, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&f, SLIDE_MS);
  lv_anim_start(&f);

  ui_feedback(UI_FB_SELECT); // soft blip

  s_timer = lv_timer_create(dismiss_cb, NOTIFY_MS, NULL);
  lv_timer_set_repeat_count(s_timer, 1);
}

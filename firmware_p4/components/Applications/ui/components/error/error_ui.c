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

#include "error_ui.h"

#include "lvgl.h"

#include "ui_feedback.h"

#define ERROR_MS  5000 // errors linger a bit longer than notifications
#define SLIDE_MS  220
#define TOP_Y     8
#define BANNER_W  224
#define COL_TXT_W 150
#define COL_ERR   0xFF3B47

static lv_obj_t *s_banner = NULL;
static lv_timer_t *s_timer = NULL;

static void slide_y_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}
static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void gone_cb(lv_anim_t *a) {
  (void)a;
  if (s_banner != NULL) {
    lv_obj_del(s_banner);
    s_banner = NULL;
  }
}

static void clear_now(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_banner != NULL) {
    lv_anim_delete(s_banner, NULL);
    lv_obj_del(s_banner);
    s_banner = NULL;
  }
}

static void dismiss_cb(lv_timer_t *t) {
  (void)t;
  s_timer = NULL;
  if (s_banner == NULL)
    return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_banner);
  lv_anim_set_exec_cb(&a, slide_y_cb);
  lv_anim_set_values(&a, 0, -60);
  lv_anim_set_duration(&a, SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&a, gone_cb);
  lv_anim_start(&a);

  lv_anim_t f;
  lv_anim_init(&f);
  lv_anim_set_var(&f, s_banner);
  lv_anim_set_exec_cb(&f, opa_cb);
  lv_anim_set_values(&f, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&f, SLIDE_MS);
  lv_anim_start(&f);
}

void error_show(const char *title, const char *msg) {
  clear_now(); // replace any current error banner

  lv_color_t err = lv_color_hex(COL_ERR);
  lv_obj_t *b = lv_obj_create(lv_layer_top());
  s_banner = b;
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE); // never steals input
  lv_obj_set_width(b, BANNER_W);
  lv_obj_set_height(b, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x1B0509), 0);
  lv_obj_set_style_bg_grad_color(b, lv_color_hex(0x2A0D12), 0);
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, err, 0);
  lv_obj_set_style_shadow_width(b, 20, 0);
  lv_obj_set_style_shadow_color(b, err, 0);
  lv_obj_set_style_shadow_spread(b, -6, 0);
  lv_obj_set_style_shadow_ofs_y(b, 6, 0);
  lv_obj_set_style_pad_all(b, 10, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 11, 0);
  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, TOP_Y);

  lv_obj_t *chip = lv_obj_create(b);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, 40, 40);
  lv_obj_set_style_radius(chip, 11, 0);
  lv_obj_set_style_bg_color(chip, err, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_set_style_pad_all(chip, 0, 0);
  lv_obj_t *x = lv_label_create(chip);
  lv_label_set_text(x, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(x, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(x, &lv_font_montserrat_16, 0);
  lv_obj_center(x);

  // Fixed-width text column so the title dots and the message wraps cleanly.
  lv_obj_t *col = lv_obj_create(b);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(col, COL_TXT_W);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 2, 0);

  lv_obj_t *t = lv_label_create(col);
  lv_obj_set_width(t, lv_pct(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_label_set_text(t, title ? title : "Error");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);

  if (msg != NULL) {
    lv_obj_t *m = lv_label_create(col);
    lv_obj_set_width(m, lv_pct(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP); // detail may wrap to 2 lines
    lv_label_set_text(m, msg);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m, lv_color_hex(0xE7B7BB), 0);
  }

  // Slide down + fade in.
  lv_obj_set_style_opa(b, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, b);
  lv_anim_set_exec_cb(&a, slide_y_cb);
  lv_anim_set_values(&a, -60, 0);
  lv_anim_set_duration(&a, SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  lv_anim_t f;
  lv_anim_init(&f);
  lv_anim_set_var(&f, b);
  lv_anim_set_exec_cb(&f, opa_cb);
  lv_anim_set_values(&f, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&f, SLIDE_MS);
  lv_anim_start(&f);

  ui_feedback(UI_FB_WRITE); // firmer cue (tone + vibration) for an error

  s_timer = lv_timer_create(dismiss_cb, ERROR_MS, NULL);
  lv_timer_set_repeat_count(s_timer, 1);
}

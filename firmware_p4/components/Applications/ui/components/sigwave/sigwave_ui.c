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

#include "sigwave_ui.h"

#include "ui_theme.h"

static const int SEG_W[] = {22, 11, 5, 6, 5, 6, 5, 14, 5, 6, 9, 6, 5, 6, 5, 6, 5};
#define SEG_COUNT   ((int)(sizeof(SEG_W) / sizeof(SEG_W[0])))
#define SIG_W       132
#define SIG_H       40
#define SIG_BASE_H  3
#define SIG_PULSE_H 26
#define SIG_STEP_MS 85
#define SIG_HOLD_MS 650

static void sig_form_cb(void *var, int32_t v) {
  lv_obj_t *holder = (lv_obj_t *)var;
  uint32_t n = lv_obj_get_child_count(holder);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *m = lv_obj_get_child(holder, i);
    int32_t local = v - (int32_t)i * 256;
    int32_t opa = local <= 0 ? 0 : (local >= 256 ? 255 : local);
    lv_obj_set_style_opa(m, (lv_opa_t)opa, 0);
  }
}

static lv_obj_t *
build_sigwave(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs, bool animate) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, SIG_W, SIG_H);
  lv_obj_align(cont, align, x_ofs, y_ofs);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);

  lv_obj_t *base = lv_obj_create(cont);
  lv_obj_remove_flag(base, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(base, SIG_W, SIG_BASE_H);
  lv_obj_set_pos(base, 0, SIG_H - SIG_BASE_H);
  lv_obj_set_style_radius(base, 0, 0);
  lv_obj_set_style_bg_color(base, current_theme.border_inactive, 0);
  lv_obj_set_style_bg_opa(base, LV_OPA_50, 0);
  lv_obj_set_style_border_width(base, 0, 0);

  lv_obj_t *holder = lv_obj_create(cont);
  lv_obj_remove_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(holder, SIG_W, SIG_H);
  lv_obj_set_pos(holder, 0, 0);
  lv_obj_set_style_bg_opa(holder, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(holder, 0, 0);
  lv_obj_set_style_pad_all(holder, 0, 0);

  int x = 0;
  int marks = 0;
  for (int i = 0; i < SEG_COUNT && x < SIG_W; i++) {
    int w = SEG_W[i];
    if (x + w > SIG_W)
      w = SIG_W - x;
    if ((i % 2) == 0 && w > 0) {
      lv_obj_t *m = lv_obj_create(holder);
      lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(m, w, SIG_PULSE_H);
      lv_obj_set_pos(m, x, SIG_H - SIG_BASE_H - SIG_PULSE_H);
      lv_obj_set_style_radius(m, 1, 0);
      lv_obj_set_style_bg_color(m, current_theme.border_accent, 0);
      lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(m, 0, 0);
      lv_obj_set_style_opa(m, animate ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
      marks++;
    }
    x += SEG_W[i];
  }
  if (marks < 1)
    marks = 1;

  if (animate) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, holder);
    lv_anim_set_exec_cb(&a, sig_form_cb);
    lv_anim_set_values(&a, 0, marks * 256);
    lv_anim_set_duration(&a, marks * SIG_STEP_MS);
    lv_anim_set_repeat_delay(&a, SIG_HOLD_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
  }

  return cont;
}

lv_obj_t *sigwave_create(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs) {
  return build_sigwave(parent, align, x_ofs, y_ofs, true);
}

lv_obj_t *sigwave_create_static(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs) {
  return build_sigwave(parent, align, x_ofs, y_ofs, false);
}

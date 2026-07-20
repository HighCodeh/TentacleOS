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

#include "page_dots_ui.h"

#include "ui_theme.h"

static const int DOT_PATTERN[] = {4, 7, 12, 7, 4};
#define PATTERN_LEN 5
#define ANIM_MS     450

static void dot_size_cb(void *var, int32_t v) {
  lv_obj_set_width((lv_obj_t *)var, v);
  lv_obj_set_height((lv_obj_t *)var, v);
}

static void dot_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void dot_animate_to(lv_obj_t *dot, int size, lv_opa_t opa) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, dot);
  lv_anim_set_duration(&a, ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

  lv_anim_set_exec_cb(&a, dot_size_cb);
  lv_anim_set_values(&a, lv_obj_get_width(dot), size);
  lv_anim_start(&a);

  lv_anim_set_exec_cb(&a, dot_opa_cb);
  lv_anim_set_values(&a, lv_obj_get_style_bg_opa(dot, 0), opa);
  lv_anim_start(&a);
}

page_dots_t page_dots_create(lv_obj_t *parent, int total, lv_align_t align, int x_ofs, int y_ofs) {
  page_dots_t pd = {0};
  pd.total = (total > PAGE_DOTS_MAX) ? PAGE_DOTS_MAX : total;
  pd.current = 0;

  pd.container = lv_obj_create(parent);
  lv_obj_set_size(pd.container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(pd.container, align, x_ofs, y_ofs);
  lv_obj_set_flex_flow(pd.container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      pd.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(pd.container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pd.container, 0, 0);
  lv_obj_set_style_pad_all(pd.container, 0, 0);
  lv_obj_set_style_pad_column(pd.container, 6, 0);
  lv_obj_remove_flag(pd.container, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < pd.total; i++) {
    pd.dots[i] = lv_obj_create(pd.container);
    lv_obj_set_size(pd.dots[i], 4, 4);
    lv_obj_set_style_radius(pd.dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(pd.dots[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pd.dots[i], current_theme.text_main, 0);
    lv_obj_set_style_border_width(pd.dots[i], 0, 0);
    lv_obj_remove_flag(pd.dots[i], LV_OBJ_FLAG_SCROLLABLE);
  }

  page_dots_set(&pd, 0);

  return pd;
}

void page_dots_set(page_dots_t *pd, int index) {
  if (!pd || index < 0 || index >= pd->total)
    return;
  pd->current = index;

  for (int i = 0; i < pd->total; i++) {
    int rel = i - index;
    int dist = rel < 0 ? -rel : rel;

    lv_anim_delete(pd->dots[i], dot_size_cb);
    lv_anim_delete(pd->dots[i], dot_opa_cb);

    if (dist <= 2) {
      bool was_hidden = lv_obj_has_flag(pd->dots[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(pd->dots[i], LV_OBJ_FLAG_HIDDEN);

      int sz = DOT_PATTERN[2 + rel];
      lv_opa_t opa = (dist == 0) ? LV_OPA_COVER : (dist == 1) ? LV_OPA_40 : LV_OPA_20;
      lv_color_t col = (dist == 0) ? current_theme.border_accent : current_theme.text_main;
      lv_obj_set_style_bg_color(pd->dots[i], col, 0);

      if (was_hidden) {
        lv_obj_set_size(pd->dots[i], sz, sz);
        lv_obj_set_style_bg_opa(pd->dots[i], opa, 0);
      } else {
        dot_animate_to(pd->dots[i], sz, opa);
      }
    } else {
      lv_obj_add_flag(pd->dots[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void page_dots_show(page_dots_t *pd) {
  if (pd && pd->container)
    lv_obj_remove_flag(pd->container, LV_OBJ_FLAG_HIDDEN);
}

void page_dots_hide(page_dots_t *pd) {
  if (pd && pd->container)
    lv_obj_add_flag(pd->container, LV_OBJ_FLAG_HIDDEN);
}

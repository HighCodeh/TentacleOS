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

#include "waves_ui.h"

#include "assets_manager.h"
#include "ui_theme.h"

#define WAVES_RING_COUNT 3
#define WAVES_MIN        30
#define WAVES_MAX        132
#define WAVES_MS         1800
#define WAVES_NODE       38
#define WAVES_ICON_PX    18
#define WAVES_CONT       (WAVES_MAX + 8)

static void waves_ring_cb(void *var, int32_t v) {
  lv_obj_t *ring = (lv_obj_t *)var;
  int32_t sz = WAVES_MIN + (WAVES_MAX - WAVES_MIN) * v / 255;
  lv_obj_set_size(ring, sz, sz);
  lv_obj_center(ring);
  lv_obj_set_style_opa(ring, (lv_opa_t)(255 - v), 0);
}

lv_obj_t *waves_create(lv_obj_t *parent,
                       lv_align_t align,
                       int x_ofs,
                       int y_ofs,
                       const char *symbol,
                       const char *icon_path) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, WAVES_CONT, WAVES_CONT);
  lv_obj_align(cont, align, x_ofs, y_ofs);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);

  for (int i = 0; i < WAVES_RING_COUNT; i++) {
    lv_obj_t *ring = lv_obj_create(cont);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, current_theme.border_accent, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(ring, WAVES_MIN, WAVES_MIN);
    lv_obj_center(ring);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, waves_ring_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, WAVES_MS);
    lv_anim_set_delay(&a, i * (WAVES_MS / WAVES_RING_COUNT));
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }

  lv_obj_t *node = lv_obj_create(cont);
  lv_obj_remove_flag(node, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(node, WAVES_NODE, WAVES_NODE);
  lv_obj_set_style_radius(node, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(node, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(node, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(node, 0, 0);
  lv_obj_center(node);

  lv_image_dsc_t *icon_dsc = icon_path ? assets_get(icon_path) : NULL;
  if (icon_dsc != NULL) {
    lv_obj_t *img = lv_image_create(node);
    lv_image_set_src(img, icon_dsc);
    int32_t longest =
        icon_dsc->header.w > icon_dsc->header.h ? icon_dsc->header.w : icon_dsc->header.h;
    if (longest > 0)
      lv_image_set_scale(img, WAVES_ICON_PX * 256 / longest);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
  } else if (symbol) {
    lv_obj_t *sym = lv_label_create(node);
    lv_label_set_text(sym, symbol);
    lv_obj_set_style_text_color(sym, current_theme.text_main, 0);
    lv_obj_center(sym);
  }

  return cont;
}

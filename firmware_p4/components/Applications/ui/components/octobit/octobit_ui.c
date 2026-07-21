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

#include "octobit_ui.h"

#include "st7789.h"

#include "assets_manager.h"
#include "ui_theme.h"

#define OCTOBIT_ASSET "/assets/img/octobit.bin"
#define OCTOBIT_W     80
#define OCTOBIT_H     115
#define OCTOBIT_SCALE 448
#define SCALED_W      (OCTOBIT_W * OCTOBIT_SCALE / 256)
#define SCALED_H      (OCTOBIT_H * OCTOBIT_SCALE / 256)

#define BALLOON_MAX_W    150
#define BALLOON_MIN_W    96
#define BALLOON_PAD      12
#define BALLOON_RADIUS   14
#define BALLOON_BORDER   2
#define BALLOON_OFFSET_X (-SCALED_W + 60)
#define BALLOON_OFFSET_Y (-SCALED_H + 30)

#define SWAY_ANGLE 40
#define SWAY_TIME  1300

#define SIGNAL_COUNT 3
#define SIGNAL_DOT   7
#define SIGNAL_X     (LCD_H_RES - SCALED_W + 20)
#define SIGNAL_Y     (LCD_V_RES - SCALED_H + 16)
#define SIGNAL_DX    (-22)
#define SIGNAL_DY    (-18)
#define SIGNAL_TIME  1100

static void signal_anim_cb(void *var, int32_t v) {
  lv_obj_t *dot = (lv_obj_t *)var;
  lv_obj_set_pos(dot, SIGNAL_X + SIGNAL_DX * v / 255, SIGNAL_Y + SIGNAL_DY * v / 255);
  lv_obj_set_style_opa(dot, (lv_opa_t)(255 - v), 0);
}

lv_obj_t *octobit_create(lv_obj_t *parent, const char *phrase) {
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, LCD_H_RES, LCD_V_RES);
  lv_obj_center(root);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *img = lv_image_create(root);
  lv_image_dsc_t *dsc = assets_get(OCTOBIT_ASSET);
  if (dsc != NULL)
    lv_image_set_src(img, dsc);
  lv_image_set_pivot(img, OCTOBIT_W, OCTOBIT_H);
  lv_image_set_scale(img, OCTOBIT_SCALE);
  lv_obj_align(img, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_anim_t sway;
  lv_anim_init(&sway);
  lv_anim_set_var(&sway, img);
  lv_anim_set_exec_cb(&sway, (lv_anim_exec_xcb_t)lv_image_set_rotation);
  lv_anim_set_values(&sway, -SWAY_ANGLE, SWAY_ANGLE);
  lv_anim_set_duration(&sway, SWAY_TIME);
  lv_anim_set_playback_duration(&sway, SWAY_TIME);
  lv_anim_set_repeat_count(&sway, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&sway, lv_anim_path_ease_in_out);
  lv_anim_start(&sway);

  for (int i = 0; i < SIGNAL_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(root);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, SIGNAL_DOT, SIGNAL_DOT);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, current_theme.border_accent, 0);

    lv_anim_t sig;
    lv_anim_init(&sig);
    lv_anim_set_var(&sig, dot);
    lv_anim_set_exec_cb(&sig, signal_anim_cb);
    lv_anim_set_values(&sig, 0, 255);
    lv_anim_set_duration(&sig, SIGNAL_TIME);
    lv_anim_set_delay(&sig, i * (SIGNAL_TIME / SIGNAL_COUNT));
    lv_anim_set_repeat_count(&sig, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&sig, lv_anim_path_linear);
    lv_anim_start(&sig);
  }

  lv_obj_t *balloon = lv_obj_create(root);
  lv_obj_remove_flag(balloon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(balloon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(balloon, BALLOON_RADIUS, 0);
  lv_obj_set_style_bg_opa(balloon, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(balloon, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(balloon, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(balloon, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(balloon, BALLOON_BORDER, 0);
  lv_obj_set_style_border_color(balloon, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(balloon, BALLOON_PAD, 0);
  lv_obj_set_style_min_width(balloon, BALLOON_MIN_W, 0);
  lv_obj_align(balloon, LV_ALIGN_BOTTOM_RIGHT, BALLOON_OFFSET_X, BALLOON_OFFSET_Y);

  lv_obj_t *lbl = lv_label_create(balloon);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lbl, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(lbl, BALLOON_MAX_W, 0);
  lv_label_set_text(lbl, phrase != NULL ? phrase : "");

  lv_obj_set_user_data(root, lbl);
  return root;
}

void octobit_set_text(lv_obj_t *octobit_root, const char *phrase) {
  if (octobit_root == NULL)
    return;
  lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(octobit_root);
  if (lbl != NULL)
    lv_label_set_text(lbl, phrase != NULL ? phrase : "");
}

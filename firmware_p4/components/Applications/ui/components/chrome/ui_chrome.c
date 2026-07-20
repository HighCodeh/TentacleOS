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

#include "ui_chrome.h"

#include "st7789.h"

#include "assets_manager.h"
#include "ui_theme.h"

// Kept in lock-step with menu_component_ui.c so the chrome on an activity
// screen is pixel-identical to the chrome on a list submenu.
#define ICON_CELL 26

static lv_font_t *chrome_font = NULL;

static lv_obj_t *make_icon(lv_obj_t *parent, const char *icon_path) {
  if (!icon_path)
    return NULL;
  lv_image_dsc_t *dsc = assets_get(icon_path);
  if (!dsc)
    return NULL;
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, dsc);
  lv_obj_set_size(img, ICON_CELL, ICON_CELL);
  lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN); // normalize to a constant box
  return img;
}

lv_obj_t *ui_chrome_header(lv_obj_t *parent, const char *title, const char *icon_path) {
  if (!chrome_font)
    chrome_font = lv_binfont_create("A:assets/fonts/Inter.bin");

  lv_obj_t *hdr = lv_obj_create(parent);
  lv_obj_set_size(hdr, LCD_H_RES, UI_CHROME_HEADER_H);
  lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(hdr, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(hdr, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_style_border_width(hdr, 2, 0);
  lv_obj_set_style_border_color(hdr, current_theme.border_accent, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);

  if (icon_path) {
    lv_obj_t *ic = make_icon(hdr, icon_path);
    if (ic)
      lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);
  }

  lv_obj_t *lbl = lv_label_create(hdr);
  lv_label_set_text(lbl, title ? title : "");
  lv_obj_set_style_text_color(lbl, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(lbl, chrome_font ? chrome_font : &lv_font_montserrat_14, 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

  return hdr;
}

lv_obj_t *ui_chrome_footer(lv_obj_t *parent, const char *hint) {
  lv_obj_t *ft = lv_obj_create(parent);
  lv_obj_set_size(ft, LCD_H_RES, UI_CHROME_FOOTER_H);
  lv_obj_align(ft, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_remove_flag(ft, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(ft, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(ft, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(ft, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ft, 0, 0);
  lv_obj_set_style_pad_all(ft, 0, 0);
  lv_obj_set_style_border_width(ft, 2, 0);
  lv_obj_set_style_border_color(ft, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(ft, LV_BORDER_SIDE_TOP, 0);

  lv_obj_t *lbl = lv_label_create(ft);
  lv_label_set_text(lbl, hint ? hint : "");
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(lbl, LV_OPA_70, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(lbl);

  return ft;
}

void ui_chrome_footer_set_text(lv_obj_t *footer, const char *hint) {
  if (!footer)
    return;
  uint32_t n = lv_obj_get_child_count(footer);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *c = lv_obj_get_child(footer, i);
    if (lv_obj_check_type(c, &lv_label_class)) {
      lv_label_set_text(c, hint ? hint : "");
      return;
    }
  }
}

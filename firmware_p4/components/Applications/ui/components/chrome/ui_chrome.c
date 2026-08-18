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

#include <stdio.h>

#include "st7789.h"

#include "header_ui.h"
#include "ui_metrics.h"
#include "ui_theme.h"

// Whether the next chrome header shows the breadcrumb letreiro. Set per screen by
// ui_chrome_set_status_enabled(): browse screens = true, active operation screens
// (reading/sending/scanning/emulating/...) = false. The status HEADER itself is
// drawn on EVERY screen regardless; only the letreiro (and the dropdown) are gated.
static bool s_status_enabled = true;

void ui_chrome_set_status_enabled(bool enabled) {
  s_status_enabled = enabled;
}

bool ui_chrome_status_enabled(void) {
  return s_status_enabled;
}

// Breadcrumb root: the most-recent menu's title. Leaf screens show "root / title";
// menus set it (showing just their own name); home/coverflow clear it.
static char s_bc_root[24] = "";

void ui_chrome_set_breadcrumb_root(const char *root) {
  snprintf(s_bc_root, sizeof(s_bc_root), "%s", root ? root : "");
}

void ui_chrome_compose_title(char *dst, size_t n, const char *title) {
  if (!dst || n == 0)
    return;
  const char *t = title ? title : "";
  if (s_bc_root[0])
    snprintf(dst, n, "%s / %s", s_bc_root, t);
  else
    snprintf(dst, n, "%s", t);
}

lv_obj_t *ui_chrome_light_title(lv_obj_t *container, const char *title) {
  if (!container)
    return NULL;
  // The dynamic status header, IDENTICAL to home/coverflow (floating card: 12:00 +
  // wifi/bt/sd/battery), on EVERY screen. Sits at the very top so its -12 float
  // clips against the display edge exactly as on home, and it (re)binds the shared
  // status statics + singleton timers.
  header_ui_create(container);
  // The breadcrumb light label rides just below the card ONLY on browse screens;
  // active operation screens show the header alone, no letreiro.
  if (!s_status_enabled)
    return NULL;
  lv_obj_t *lbl = lv_label_create(container);
  lv_label_set_text(lbl, title ? title : "");
  lv_obj_set_style_text_color(lbl, current_theme.border_accent, 0);
  lv_obj_set_style_text_opa(lbl, LV_OPA_80, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl, ui_screen_w() - 24);
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 12, -1);
  return lbl;
}

// Transparent top-area container of the standard header height, so screen content
// still starts at UI_CHROME_HEADER_H and no per-screen layout changes are needed.
static lv_obj_t *make_header_container(lv_obj_t *parent) {
  lv_obj_t *hdr = lv_obj_create(parent);
  lv_obj_set_size(hdr, lv_pct(100), UI_CHROME_HEADER_H);
  lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  return hdr;
}

lv_obj_t *ui_chrome_header(lv_obj_t *parent, const char *title, const char *icon_path) {
  (void)icon_path; // the shared status header carries its own icons now
  char composed[48];
  ui_chrome_compose_title(composed, sizeof(composed), title);
  lv_obj_t *hdr = make_header_container(parent);
  ui_chrome_light_title(hdr, composed);
  return hdr;
}

lv_obj_t *ui_chrome_header_overlay(lv_obj_t *parent, const char *title, const char *icon_path) {
  (void)title;
  (void)icon_path;
  // Transient overlay drawn over a live screen: a STATIC snapshot header (no
  // globals, no timers, no letreiro), so tearing the overlay down never dangles
  // the dynamic header of the screen underneath.
  lv_obj_t *hdr = make_header_container(parent);
  header_ui_create_snapshot(hdr);
  return hdr;
}

lv_obj_t *ui_chrome_footer(lv_obj_t *parent, const char *hint) {
  lv_obj_t *ft = lv_obj_create(parent);
  lv_obj_set_size(ft, lv_pct(100), UI_CHROME_FOOTER_H);
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

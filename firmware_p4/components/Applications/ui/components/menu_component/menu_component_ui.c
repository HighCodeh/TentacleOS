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

#include "menu_component_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define HEADER_BG   current_theme.bg_secondary
#define HEADER_LINE current_theme.border_accent
#define FOOTER_LINE current_theme.border_interface
#define TITLE_COLOR current_theme.border_accent
#define ITEM_BG     current_theme.bg_secondary
#define ITEM_BORDER current_theme.border_inactive
#define SEL_BORDER  current_theme.border_accent

#define HEADER_H     46 // a touch taller so the submenu breadcrumb label has room below the header
#define FOOTER_H     MENU_COMP_FOOTER_H
#define ITEM_H       44
#define ITEM_GAP     6
#define ICON_CELL    26
#define LEFT_MARGIN  6
#define RIGHT_GUTTER 16
#define ITEMS_Y      (HEADER_H + 4)
#define OUTER_BORDER 4
#define THUMB_FALLBACK_H 45
#define SCROLL_ANIM_MS   200
#define OVERFLOW_SLOP_PX 2

#define DEFAULT_HINT \
  LV_SYMBOL_UP LV_SYMBOL_DOWN "  Nav    " LV_SYMBOL_OK "  OK    " LV_SYMBOL_LEFT "  Back"

static lv_obj_t *make_icon_dsc(lv_obj_t *parent, const lv_image_dsc_t *dsc) {
  if (!dsc)
    return NULL;
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, dsc);
  lv_obj_set_size(img, ICON_CELL, ICON_CELL);
  lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
  return img;
}

static lv_obj_t *make_icon(lv_obj_t *parent, const char *icon_path) {
  if (!icon_path)
    return NULL;
  return make_icon_dsc(parent, assets_get(icon_path));
}

static bool list_overflows(menu_component_t *m) {
  if (!m || !m->items_cont)
    return false;
  lv_obj_update_layout(m->items_cont);
  int32_t st = lv_obj_get_scroll_top(m->items_cont);
  int32_t sb = lv_obj_get_scroll_bottom(m->items_cont);
  return (st + sb) > OVERFLOW_SLOP_PX;
}

static void update_scroll_state(menu_component_t *m) {
  if (!m || !m->items_cont)
    return;
  bool overflow = list_overflows(m);
  if (m->scroll_track)
    lv_obj_remove_flag(m->scroll_track, LV_OBJ_FLAG_HIDDEN);
  if (m->scroll_bar)
    lv_obj_remove_flag(m->scroll_bar, LV_OBJ_FLAG_HIDDEN);
  if (overflow) {
    lv_obj_add_flag(m->items_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_snap_y(m->items_cont, LV_SCROLL_SNAP_NONE);
  } else {
    lv_obj_remove_flag(m->items_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_snap_y(m->items_cont, LV_SCROLL_SNAP_NONE);
    lv_obj_scroll_to_y(m->items_cont, 0, LV_ANIM_OFF);
  }
}

static void update_scroll_bar(menu_component_t *m) {
  if (!m->scroll_bar || m->item_count <= 1)
    return;
  int32_t thumb_h = lv_obj_get_height(m->scroll_bar);
  if (thumb_h <= 0)
    thumb_h = THUMB_FALLBACK_H;
  int32_t travel = m->track_h - thumb_h;
  if (travel < 0)
    travel = 0;
  int32_t pos = m->track_y_start + (m->selected * travel) / (m->item_count - 1);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, m->scroll_bar);
  lv_anim_set_values(&a, lv_obj_get_y(m->scroll_bar), pos);
  lv_anim_set_duration(&a, SCROLL_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&a);
}

static void update_selection(menu_component_t *m) {
  for (int i = 0; i < m->item_count; i++) {
    if (i == m->selected) {
      lv_obj_set_style_border_color(m->items[i], SEL_BORDER, 0);
      bool has_widget = m->has_toggle[i] || m->has_intensity[i] || m->val_labels[i];
      if (m->sel_dots[i]) {
        if (has_widget)
          lv_obj_add_flag(m->sel_dots[i], LV_OBJ_FLAG_HIDDEN);
        else
          lv_obj_remove_flag(m->sel_dots[i], LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_obj_set_style_border_color(m->items[i], ITEM_BORDER, 0);
      if (m->sel_dots[i])
        lv_obj_add_flag(m->sel_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (m->items[m->selected] && list_overflows(m)) {
    lv_obj_scroll_to_view(m->items[m->selected], LV_ANIM_ON);
  }

  update_scroll_bar(m);
}

menu_component_t
menu_component_create(lv_obj_t *parent, const char *title, const char *title_icon_path) {
  menu_component_t m = {0};
  (void)title_icon_path; // the shared status header carries its own icons now

  m.screen = lv_obj_create(parent);
  lv_obj_set_size(m.screen, lv_pct(100), lv_pct(100));
  lv_obj_align(m.screen, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_remove_flag(m.screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(m.screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(m.screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(m.screen, 0, 0);
  lv_obj_set_style_border_width(m.screen, 0, 0);
  lv_obj_set_style_radius(m.screen, 0, 0);

  // Every menu carries the dynamic home header; ui_chrome_light_title adds the
  // breadcrumb letreiro only on browse screens (operation menus get the header
  // alone). Kept in title_bar (transparent) so callers that fade_in(title_bar)
  // still animate the whole area.
  m.title_bar = lv_obj_create(m.screen);
  lv_obj_set_size(m.title_bar, lv_pct(100), HEADER_H);
  lv_obj_align(m.title_bar, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_remove_flag(m.title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(m.title_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(m.title_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m.title_bar, 0, 0);
  lv_obj_set_style_pad_all(m.title_bar, 0, 0);
  lv_obj_set_style_radius(m.title_bar, 0, 0);
  m.title_label = ui_chrome_light_title(m.title_bar, title);

  int items_h = ui_screen_h() - ITEMS_Y - FOOTER_H - 4;
  if (items_h < ITEM_H)
    items_h = ITEM_H;

  m.items_cont = lv_obj_create(m.screen);
  lv_obj_set_size(m.items_cont, ui_screen_w() - LEFT_MARGIN - RIGHT_GUTTER, items_h);
  lv_obj_align(m.items_cont, LV_ALIGN_TOP_LEFT, LEFT_MARGIN, ITEMS_Y);
  lv_obj_set_style_bg_opa(m.items_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m.items_cont, 0, 0);
  lv_obj_set_style_pad_all(m.items_cont, 2, 0);
  lv_obj_set_style_pad_row(m.items_cont, ITEM_GAP, 0);
  lv_obj_set_flex_flow(m.items_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(m.items_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_snap_y(m.items_cont, LV_SCROLL_SNAP_NONE);

  int track_x = ui_screen_w() - OUTER_BORDER - 9;
  m.track_y_start = ITEMS_Y + 8;
  m.track_h = items_h - 16;
  if (m.track_h < 0)
    m.track_h = 0;

  static lv_point_precise_t track_pts[2];
  track_pts[0].x = 0;
  track_pts[0].y = 0;
  track_pts[1].x = 0;
  track_pts[1].y = m.track_h;

  m.scroll_track = lv_line_create(m.screen);
  lv_line_set_points(m.scroll_track, track_pts, 2);
  lv_obj_set_pos(m.scroll_track, track_x, m.track_y_start);
  lv_obj_set_style_line_color(m.scroll_track, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(m.scroll_track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_width(m.scroll_track, 3, 0);
  lv_obj_set_style_line_dash_width(m.scroll_track, 4, 0);
  lv_obj_set_style_line_dash_gap(m.scroll_track, 4, 0);

  static lv_image_dsc_t *slide_bar_v_dsc = NULL;
  if (!slide_bar_v_dsc)
    slide_bar_v_dsc = assets_get("/assets/icons/drag_indicator.bin");

  m.scroll_bar = lv_image_create(m.screen);
  if (slide_bar_v_dsc)
    lv_image_set_src(m.scroll_bar, slide_bar_v_dsc);
  lv_obj_set_pos(m.scroll_bar, track_x - 4, m.track_y_start);
  lv_obj_move_foreground(m.scroll_bar);

  m.footer = lv_obj_create(m.screen);
  lv_obj_set_size(m.footer, lv_pct(100), FOOTER_H);
  lv_obj_align(m.footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_remove_flag(m.footer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(m.footer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(m.footer, HEADER_BG, 0);
  lv_obj_set_style_bg_opa(m.footer, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(m.footer, 0, 0);
  lv_obj_set_style_pad_all(m.footer, 0, 0);
  lv_obj_set_style_border_width(m.footer, 2, 0);
  lv_obj_set_style_border_color(m.footer, FOOTER_LINE, 0);
  lv_obj_set_style_border_side(m.footer, LV_BORDER_SIDE_TOP, 0);

  m.hint_label = lv_label_create(m.footer);
  lv_label_set_text(m.hint_label, DEFAULT_HINT);
  lv_obj_set_style_text_color(m.hint_label, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(m.hint_label, LV_OPA_70, 0);
  lv_obj_set_style_text_font(m.hint_label, &lv_font_montserrat_12, 0);
  lv_obj_center(m.hint_label);
  lv_obj_move_foreground(m.footer);

  m.item_count = 0;
  m.selected = 0;

  // This list is a menu/category level: its title becomes the breadcrumb root, so
  // leaf screens opened from it render their title as "title / leaf".
  ui_chrome_set_breadcrumb_root(title);

  return m;
}

// Build the item container (styling + flex). The icon (if any) is created by the
// caller next, so it lands before the label in flex order; then item_finish adds
// the label, the selection pointer, and registers the row.
static lv_obj_t *item_begin(menu_component_t *menu) {
  lv_obj_t *item = lv_obj_create(menu->items_cont);
  lv_obj_set_width(item, lv_pct(100));
  lv_obj_set_height(item, ITEM_H);
  lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(item, 10, 0);
  lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(item, ITEM_BG, 0);
  lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(item, 2, 0);
  lv_obj_set_style_border_color(item, ITEM_BORDER, 0);
  lv_obj_set_style_pad_left(item, 6, 0);
  lv_obj_set_style_pad_right(item, 8, 0);
  lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(item, 8, 0);
  return item;
}

static lv_obj_t *item_finish(menu_component_t *menu, lv_obj_t *item, const char *label) {
  lv_obj_t *lbl = lv_label_create(item);
  lv_label_set_text(lbl, label ? label : "");
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_flex_grow(lbl, 1);
  lv_obj_set_style_pad_left(lbl, 0, 0);

  static lv_image_dsc_t *pointer_dsc = NULL;
  if (!pointer_dsc)
    pointer_dsc = assets_get("/assets/icons/chevron_right.bin");

  lv_obj_t *ptr = lv_image_create(item);
  if (pointer_dsc)
    lv_image_set_src(ptr, pointer_dsc);
  lv_obj_add_flag(ptr, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
  lv_obj_align(ptr, LV_ALIGN_RIGHT_MID, -6, 0);

  int idx = menu->item_count;
  menu->items[idx] = item;
  menu->sel_dots[idx] = ptr;
  menu->val_labels[idx] = NULL;
  menu->has_toggle[idx] = false;
  menu->has_intensity[idx] = false;
  menu->item_count++;

  if (idx == menu->selected) {
    lv_obj_set_style_border_color(item, SEL_BORDER, 0);
    lv_obj_remove_flag(ptr, LV_OBJ_FLAG_HIDDEN);
  }

  update_scroll_state(menu);

  return item;
}

lv_obj_t *
menu_component_add_item(menu_component_t *menu, const char *icon_path, const char *label) {
  if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS)
    return NULL;
  lv_obj_t *item = item_begin(menu);
  make_icon(item, icon_path);
  return item_finish(menu, item, label);
}

lv_obj_t *
menu_component_add_item_dsc(menu_component_t *menu, const lv_image_dsc_t *dsc, const char *label) {
  if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS)
    return NULL;
  lv_obj_t *item = item_begin(menu);
  make_icon_dsc(item, dsc);
  return item_finish(menu, item, label);
}

lv_obj_t *menu_component_add_selector(menu_component_t *menu,
                                      const char *icon_path,
                                      const char *label,
                                      const char *initial_value) {
  if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS)
    return NULL;
  int idx = menu->item_count;

  lv_obj_t *item = menu_component_add_item(menu, icon_path, label);
  if (!item)
    return NULL;

  lv_obj_t *val = lv_label_create(item);
  lv_label_set_text_fmt(
      val, LV_SYMBOL_LEFT " %s " LV_SYMBOL_RIGHT, initial_value ? initial_value : "");
  lv_obj_set_style_text_color(val, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  lv_obj_add_flag(val, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(val, LV_ALIGN_RIGHT_MID, -6, 0);

  menu->val_labels[idx] = val;
  lv_obj_add_flag(menu->sel_dots[idx], LV_OBJ_FLAG_HIDDEN);

  return item;
}

void menu_component_set_selector_value(menu_component_t *menu, int index, const char *value) {
  if (!menu || index < 0 || index >= menu->item_count || !menu->val_labels[index])
    return;
  lv_label_set_text_fmt(
      menu->val_labels[index], LV_SYMBOL_LEFT " %s " LV_SYMBOL_RIGHT, value ? value : "");
}

lv_obj_t *menu_component_add_toggle(menu_component_t *menu,
                                    const char *icon_path,
                                    const char *label,
                                    bool initial_state) {
  if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS)
    return NULL;
  int idx = menu->item_count;

  lv_obj_t *item = menu_component_add_item(menu, icon_path, label);
  if (!item)
    return NULL;

  toggle_ui_create(&menu->toggles[idx], item);
  lv_obj_add_flag(menu->toggles[idx].obj, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(menu->toggles[idx].obj, LV_ALIGN_RIGHT_MID, -6, 0);
  toggle_ui_set(&menu->toggles[idx], initial_state);
  menu->has_toggle[idx] = true;
  lv_obj_add_flag(menu->sel_dots[idx], LV_OBJ_FLAG_HIDDEN);

  return item;
}

void menu_component_toggle_item(menu_component_t *menu, int index) {
  if (!menu || index < 0 || index >= menu->item_count || !menu->has_toggle[index])
    return;
  toggle_ui_toggle(&menu->toggles[index]);
}

bool menu_component_get_toggle(menu_component_t *menu, int index) {
  if (!menu || index < 0 || index >= menu->item_count || !menu->has_toggle[index])
    return false;
  return toggle_ui_get(&menu->toggles[index]);
}

void menu_component_set_toggle(menu_component_t *menu, int index, bool state) {
  if (!menu || index < 0 || index >= menu->item_count || !menu->has_toggle[index])
    return;
  toggle_ui_set(&menu->toggles[index], state);
}

lv_obj_t *menu_component_add_intensity(menu_component_t *menu,
                                       const char *icon_path,
                                       const char *label,
                                       int initial_level) {
  if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS)
    return NULL;
  int idx = menu->item_count;

  lv_obj_t *item = menu_component_add_item(menu, icon_path, label);
  if (!item)
    return NULL;

  intensity_bar_create(&menu->intensities[idx], item);
  lv_obj_add_flag(menu->intensities[idx].obj, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(menu->intensities[idx].obj, LV_ALIGN_RIGHT_MID, -6, 0);
  intensity_bar_set(&menu->intensities[idx], initial_level);
  menu->has_intensity[idx] = true;
  lv_obj_add_flag(menu->sel_dots[idx], LV_OBJ_FLAG_HIDDEN);

  return item;
}

void menu_component_intensity_inc(menu_component_t *menu, int index) {
  if (!menu || index < 0 || index >= menu->item_count || !menu->has_intensity[index])
    return;
  intensity_bar_inc(&menu->intensities[index]);
}

void menu_component_intensity_dec(menu_component_t *menu, int index) {
  if (!menu || index < 0 || index >= menu->item_count || !menu->has_intensity[index])
    return;
  intensity_bar_dec(&menu->intensities[index]);
}

int menu_component_get_intensity(menu_component_t *menu, int index) {
  if (!menu || index < 0 || index >= menu->item_count || !menu->has_intensity[index])
    return 0;
  return intensity_bar_get(&menu->intensities[index]);
}

void menu_component_select(menu_component_t *menu, int index) {
  if (!menu || index < 0 || index >= menu->item_count)
    return;
  menu->selected = index;
  update_selection(menu);
}

void menu_component_next(menu_component_t *menu) {
  if (!menu || menu->item_count == 0)
    return;
  menu->selected = (menu->selected + 1) % menu->item_count;
  update_selection(menu);
  ui_feedback(UI_FB_NAV);
}

void menu_component_prev(menu_component_t *menu) {
  if (!menu || menu->item_count == 0)
    return;
  menu->selected = (menu->selected == 0) ? menu->item_count - 1 : menu->selected - 1;
  update_selection(menu);
  ui_feedback(UI_FB_NAV);
}

int menu_component_get_selected(menu_component_t *menu) {
  return menu ? menu->selected : -1;
}

void menu_component_set_item_label_color(menu_component_t *menu, int index, lv_color_t color) {
  if (!menu || index < 0 || index >= menu->item_count)
    return;
  lv_obj_t *item = menu->items[index];
  if (!item)
    return;
  uint32_t n = lv_obj_get_child_count(item);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *child = lv_obj_get_child(item, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      lv_obj_set_style_text_color(child, color, 0);
      return;
    }
  }
}

void menu_component_set_hint(menu_component_t *menu, const char *text) {
  if (!menu || !menu->hint_label)
    return;
  lv_label_set_text(menu->hint_label, text ? text : "");
}

void menu_component_add_section(menu_component_t *menu, const char *title) {
  if (!menu || !menu->items_cont)
    return;
  lv_obj_t *sec = lv_label_create(menu->items_cont);
  lv_label_set_text(sec, title ? title : "");
  lv_obj_set_width(sec, lv_pct(100));
  lv_obj_set_style_text_align(sec, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(sec, current_theme.border_accent, 0);
  lv_obj_set_style_text_opa(sec, LV_OPA_60, 0);
  lv_obj_set_style_text_font(sec, &lv_font_montserrat_12, 0);
  lv_obj_set_style_pad_top(sec, 6, 0);
  lv_obj_set_style_pad_bottom(sec, 1, 0);
}

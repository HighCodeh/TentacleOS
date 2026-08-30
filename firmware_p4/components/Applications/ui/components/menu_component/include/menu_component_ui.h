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

#ifndef UI_MENU_COMPONENT_H
#define UI_MENU_COMPONENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#include "toggle_ui.h"
#include "intensity_bar_ui.h"

/** @brief Maximum number of items a menu can hold. */
#define MENU_COMP_MAX_ITEMS 20

/** @brief Height in px of the persistent action/hint footer drawn at the bottom of every menu. */
#define MENU_COMP_FOOTER_H 22

/**
 * @brief State and widget handles for a full menu screen.
 */
typedef struct {
  lv_obj_t *screen;
  lv_obj_t *title_bar;
  lv_obj_t *title_label;
  lv_obj_t *items_cont;
  lv_obj_t *items[MENU_COMP_MAX_ITEMS];
  lv_obj_t *scroll_track;
  lv_obj_t *scroll_bar;
  lv_obj_t *footer;     ///< persistent action/hint bar at the bottom
  lv_obj_t *hint_label; ///< centered text inside the footer
  lv_obj_t *sel_dots[MENU_COMP_MAX_ITEMS];
  lv_obj_t *val_labels[MENU_COMP_MAX_ITEMS];
  toggle_ui_t toggles[MENU_COMP_MAX_ITEMS];
  bool has_toggle[MENU_COMP_MAX_ITEMS];
  intensity_bar_t intensities[MENU_COMP_MAX_ITEMS];
  bool has_intensity[MENU_COMP_MAX_ITEMS];
  int track_y_start;
  int track_h;
  int item_count;
  int selected;
} menu_component_t;

/** @brief Create the full menu screen layout with title and scrollbar. */
menu_component_t
menu_component_create(lv_obj_t *parent, const char *title, const char *title_icon_path);

/** @brief Add a menu item. Returns the item object for customization. */
lv_obj_t *menu_component_add_item(menu_component_t *menu, const char *icon_path, const char *label);

/**
 * @brief Add a menu item whose icon is an in-RAM image descriptor (not an asset
 *        path). Used for icons embedded in .hb app bundles. The descriptor and
 *        its pixels must outlive the menu (the caller owns that memory).
 */
lv_obj_t *menu_component_add_item_dsc(menu_component_t *menu, const lv_image_dsc_t *dsc,
                                      const char *label);

/**
 * @brief Add a centered, non-selectable group header (e.g. "Sound & Vibration")
 *        into the list. Navigation skips it; it just visually groups the items
 *        added after it. Call it before the items that belong to the group.
 */
void menu_component_add_section(menu_component_t *menu, const char *title);

/** @brief Add a selector item with left/right value navigation. */
lv_obj_t *menu_component_add_selector(menu_component_t *menu,
                                      const char *icon_path,
                                      const char *label,
                                      const char *initial_value);

/** @brief Update the displayed value of a selector item. */
void menu_component_set_selector_value(menu_component_t *menu, int index, const char *value);

/** @brief Add a toggle item with ON/OFF switch. */
lv_obj_t *menu_component_add_toggle(menu_component_t *menu,
                                    const char *icon_path,
                                    const char *label,
                                    bool initial_state);

/** @brief Toggle the switch state of a toggle item. */
void menu_component_toggle_item(menu_component_t *menu, int index);

/** @brief Get the current toggle state of an item. */
bool menu_component_get_toggle(menu_component_t *menu, int index);

/** @brief Set the toggle state of an item. */
void menu_component_set_toggle(menu_component_t *menu, int index, bool state);

/** @brief Add an intensity bar item. */
lv_obj_t *menu_component_add_intensity(menu_component_t *menu,
                                       const char *icon_path,
                                       const char *label,
                                       int initial_level);

/** @brief Increment the intensity bar level for an item. */
void menu_component_intensity_inc(menu_component_t *menu, int index);

/** @brief Decrement the intensity bar level for an item. */
void menu_component_intensity_dec(menu_component_t *menu, int index);

/** @brief Get the intensity bar level for an item. */
int menu_component_get_intensity(menu_component_t *menu, int index);

/** @brief Select a menu item by index. */
void menu_component_select(menu_component_t *menu, int index);

/** @brief Move selection to the next menu item. */
void menu_component_next(menu_component_t *menu);

/** @brief Move selection to the previous menu item. */
void menu_component_prev(menu_component_t *menu);

/** @brief Get the currently selected menu item index. */
int menu_component_get_selected(menu_component_t *menu);

/**
 * @brief Recolour the label text of a specific menu item. Used by the
 *        Wi-Fi scan screen to render scanned SSIDs in green so they
 *        read as "captured" rather than just menu rows.
 */
void menu_component_set_item_label_color(menu_component_t *menu, int index, lv_color_t color);

/**
 * @brief Override the footer hint text (e.g. "LEFT/RIGHT change   OK toggle").
 *        The component shows a sensible default; call this to specialize it.
 */
void menu_component_set_hint(menu_component_t *menu, const char *text);

#ifdef __cplusplus
}
#endif

#endif // UI_MENU_COMPONENT_H

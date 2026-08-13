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

#ifndef UI_CHROME_H
#define UI_CHROME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "lvgl.h"

/**
 * @brief Standardized screen chrome — the SAME header + footer the
 *        menu_component draws on list submenus.
 *
 * Exposed so hand-rolled "activity" screens (NFC/RFID read/write/emulate
 * animations, etc.) get the identical look. Place the screen's own content
 * between them (top = UI_CHROME_HEADER_H, bottom = UI_CHROME_FOOTER_H). Both
 * span the full width and re-derive from LCD_H_RES, so they follow rotation.
 */

#define UI_CHROME_HEADER_H 42 ///< height of the chrome header bar, in pixels
#define UI_CHROME_FOOTER_H 22 ///< height of the chrome footer bar, in pixels

/**
 * @brief Full-width top bar: raised surface, icon pinned in the left corner
 *        (optional; NULL to omit), centered accent title, accent underline.
 *
 * @param parent     Screen/container to attach the header to.
 * @param title      Title text shown centered (NULL for none).
 * @param icon_path  ".bin" asset path for the left-corner icon (NULL to omit).
 * @return The header object.
 */
lv_obj_t *ui_chrome_header(lv_obj_t *parent, const char *title, const char *icon_path);

/**
 * @brief Header for a TRANSIENT overlay drawn over a live screen.
 *
 * Same look as ui_chrome_header but with a STATIC snapshot status header (no
 * global rebind, no timers) and never a letreiro — so tearing the overlay down
 * cannot dangle the dynamic header of the screen underneath. Use for full-screen
 * action overlays (transmit/emulate/now-playing/detail popups), NOT for regular
 * navigable screens.
 */
lv_obj_t *ui_chrome_header_overlay(lv_obj_t *parent, const char *title, const char *icon_path);

/**
 * @brief Enable/disable the shared status cluster inside the next chrome headers.
 *
 * The UI manager sets this per screen before building it: browse screens enable
 * it (so the header carries the global wifi/bt/sd/battery status bar), active
 * "operation" screens (reading/sending/scanning/emulating) disable it so their
 * chrome header stays a plain title bar. Default is enabled.
 *
 * @param enabled  true to attach status icons, false for a plain title bar.
 */
void ui_chrome_set_status_enabled(bool enabled);

/** @brief Whether the next chrome header carries the shared status bar (browse). */
bool ui_chrome_status_enabled(void);

/**
 * @brief Set the breadcrumb root (the category/menu name) for the light labels.
 *
 * Menus call this with their own title; subsequent leaf screens then render their
 * title as "root / title" (e.g. "NFC / EMULATE"). Cleared on home/coverflow.
 */
void ui_chrome_set_breadcrumb_root(const char *root);

/**
 * @brief Compose a display title from the current breadcrumb root + @p title.
 *
 * Writes "root / title" when a root is set, else just "title", into @p dst.
 */
void ui_chrome_compose_title(char *dst, size_t n, const char *title);

/**
 * @brief Fill a top-area container with the EXACT home/coverflow status header
 *        plus a light breadcrumb label naming the current submenu.
 *
 * Shared by ui_chrome_header() and the menu_component title area so every browse
 * screen shows the identical status bar (12:00 + wifi/bt/sd/battery floating
 * card) with a thin submenu label beneath it. @p container must sit at the top of
 * the screen (its child card floats -12px and clips against the display edge).
 *
 * @param container  Transparent top-area object, height UI_CHROME_HEADER_H.
 * @param title      Submenu name shown in the light label.
 * @return The light label object.
 */
lv_obj_t *ui_chrome_light_title(lv_obj_t *container, const char *title);

/**
 * @brief Full-width bottom bar: raised surface, top accent border, centered
 *        dimmed hint text (the button instructions).
 *
 * @param parent  Screen/container to attach the footer to.
 * @param hint    Hint text shown centered (NULL for none).
 * @return The footer object.
 */
lv_obj_t *ui_chrome_footer(lv_obj_t *parent, const char *hint);

/**
 * @brief Update the hint text of a footer returned by ui_chrome_footer().
 *
 * @param footer  Footer object returned by ui_chrome_footer().
 * @param hint    New hint text (NULL clears it).
 */
void ui_chrome_footer_set_text(lv_obj_t *footer, const char *hint);

#ifdef __cplusplus
}
#endif

#endif // UI_CHROME_H

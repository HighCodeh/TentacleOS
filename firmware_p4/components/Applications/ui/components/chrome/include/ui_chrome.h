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

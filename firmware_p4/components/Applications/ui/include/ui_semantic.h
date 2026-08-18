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

#ifndef UI_SEMANTIC_H
#define UI_SEMANTIC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ui_semantic.h
 * @brief Semantic UI colors — meaning-bound, not theme-bound.
 *
 * These express a fixed meaning ("this is good", "this is dangerous") that must
 * read the same in every theme, so they are deliberately hardcoded rather than
 * pulled from current_theme. Use them with lv_color_hex(UI_COL_*).
 *
 * Rule of thumb:
 *   - Structural color (background, border, primary/secondary text): use current_theme.*.
 *   - Meaning color (success/danger/warning): use one of these.
 *   - Screen-specific art (game sprites, data-series palette): keep it local; it is neither.
 */

/** @brief Good / connected / on / signal-present (green). */
#define UI_COL_SUCCESS 0x00E676

/** @brief Bad / threat / stop / failure (red). */
#define UI_COL_DANGER  0xFF5252

/** @brief Caution / pending / degraded (amber). */
#define UI_COL_WARNING 0xFFB300

/** @brief Max-contrast text/fill, absolute by intent. */
#define UI_COL_WHITE   0xFFFFFF

/** @brief Absolute black, absolute by intent. */
#define UI_COL_BLACK   0x000000

#ifdef __cplusplus
}
#endif

#endif // UI_SEMANTIC_H

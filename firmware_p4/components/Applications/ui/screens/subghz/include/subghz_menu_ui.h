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

#ifndef UI_SUBGHZ_MENU_H
#define UI_SUBGHZ_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Open the Sub-GHz menu screen (MOCK): Read / Read RAW / Analyzer / Brute / Saved. No
 * radio. */
void ui_subghz_menu_open(void);

/**
 * @brief Open the Sub-GHz "Read" capture screen (MOCK): scanning waves -> canned
 *        captured signal -> save prompt. No radio.
 */
void ui_subghz_read_open(void);

#ifdef __cplusplus
}
#endif

#endif // UI_SUBGHZ_MENU_H

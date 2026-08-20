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

#ifndef GB_UI_H
#define GB_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the Game Boy ROM picker screen.
 *
 * Recursively lists every .gb/.gbc ROM on the SD card and lets the user choose
 * one; OK hands the chosen ROM to the emulator (components/gameboy), which takes
 * over the display. Returns to the games menu on BACK or when the emulator exits.
 */
void ui_gb_open(void);

#ifdef __cplusplus
}
#endif

#endif // GB_UI_H

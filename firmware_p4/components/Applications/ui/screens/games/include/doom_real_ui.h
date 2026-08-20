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

#ifndef DOOM_REAL_UI_H
#define DOOM_REAL_UI_H

#ifdef __cplusplus
extern "C" {
#endif

// Open official DOOM (doomgeneric): shows a black loading screen, then hands the
// ST7789 to the DOOM task (see components/doom). Quitting DOOM reboots the
// device (hold OK+BACK ~2s), so this open fn never returns control to the UI.
void ui_doom_real_open(void);

#ifdef __cplusplus
}
#endif

#endif // DOOM_REAL_UI_H

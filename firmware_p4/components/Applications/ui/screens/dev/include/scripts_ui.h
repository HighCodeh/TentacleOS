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

#ifndef SCRIPTS_UI_H
#define SCRIPTS_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Open the Scripts app: a mock JS-script runner.
 *
 * Browse a static list of mock scripts (each with capability badges and a
 * green terminal preview), grant permission for dangerous capabilities, then
 * watch a mock streaming run finish in a success or error state. All data is
 * mock; no real script engine or backend is involved. */
void ui_scripts_open(void);

#ifdef __cplusplus
}
#endif

#endif // SCRIPTS_UI_H

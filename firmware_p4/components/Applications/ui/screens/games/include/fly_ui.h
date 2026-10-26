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

#ifndef FLY_UI_H
#define FLY_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/** Open the native Mosca TEST view; no Mac connection is required to navigate.
 */
void ui_fly_open(void);

/** Stop presentation and close its session without waiting for a host reply. */
void ui_fly_stop(void);

#ifdef __cplusplus
}
#endif

#endif // FLY_UI_H

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

#ifndef OCTOBIT_STATUS_UI_H
#define OCTOBIT_STATUS_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the Octobit status screen (reached with LEFT from home).
 *
 * Top: the octobit "character sheet" — portrait avatar + level badge + XP bar.
 * Below: a statistics selector navigated with UP/DOWN. A subset of the stats is
 * live (uptime, battery, heap, storage, boot count); the rest are mock.
 * BACK or RIGHT returns to home.
 */
void ui_octobit_status_open(void);

#ifdef __cplusplus
}
#endif

#endif // OCTOBIT_STATUS_UI_H

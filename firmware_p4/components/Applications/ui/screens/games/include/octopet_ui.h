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

/**
 * @file octopet_ui.h
 * @brief Octo-Pet, a Tamagotchi-style virtual pet built around the octobit mascot.
 *
 * Stats (Hunger/Happy/Energy/Clean) decay over time; Feed/Play/Sleep/Clean
 * actions keep it alive. Neglect it and it faints (revive with OK).
 */

#ifndef UI_OCTOPET_H
#define UI_OCTOPET_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the Octo-Pet virtual-pet screen.
 */
void ui_octopet_open(void);

#ifdef __cplusplus
}
#endif

#endif // UI_OCTOPET_H

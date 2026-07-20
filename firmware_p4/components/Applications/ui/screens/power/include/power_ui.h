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

#ifndef POWER_UI_H
#define POWER_UI_H

/**
 * @brief Open the power / battery (BQ25896) screen.
 *
 * Live telemetry, charge toggle, I2C scan, and software power-off (BATFET ship
 * mode).
 */
void ui_power_open(void);

#endif // POWER_UI_H

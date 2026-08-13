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

#ifndef LORA_POSITION_UI_H
#define LORA_POSITION_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the LoRa position screen — a coordinate editor for the manual
 *        fix. LAT/LON/ALT rows read out the fixed position with the active
 *        field lit and a caret; a broadcast toggle and peer count sit below.
 *        UP/DOWN pick the field, OK toggles broadcast, BACK returns to chat.
 */
void ui_lora_position_open(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_POSITION_UI_H

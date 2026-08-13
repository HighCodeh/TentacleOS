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

#ifndef LORA_TELEMETRY_UI_H
#define LORA_TELEMETRY_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the LoRa telemetry screen — a live radio monitor. Streaming
 *        sparklines breathe for channel-util and air-TX-util, a slim battery
 *        bar shows charge, and neighbours reduce to SNR chips. UP/DOWN pick a
 *        neighbour, OK opens the node, BACK returns to the mesh chat.
 */
void ui_lora_telemetry_open(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_TELEMETRY_UI_H

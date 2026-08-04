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

#ifndef LORA_CHANNELS_UI_H
#define LORA_CHANNELS_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the LoRa channels screen — a cipher rack of key slots. Each
 *        filled slot is a tiny cipher card (role dot, masked key stub and an
 *        entropy-bar signature); empty slots read as dashed blanks. UP/DOWN
 *        move the selection, OK edits, BACK returns to the mesh chat.
 */
void ui_lora_channels_open(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_CHANNELS_UI_H

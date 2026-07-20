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

#ifndef LORA_CHAT_UI_H
#define LORA_CHAT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the LoRa chat screen — peer-to-peer text messaging between
 *        HighBoy units over the SX1262 radio. OK composes a message, BACK
 *        exits, UP/DOWN scroll the conversation.
 */
void ui_lora_chat_open(void);

/**
 * @brief Open the LoRa screen directly on the chat conversation view (linked),
 *        skipping the protocol picker. Used as the boot landing screen.
 */
void ui_lora_chat_open_chat(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_CHAT_UI_H

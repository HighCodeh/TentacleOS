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

#ifndef LORA_TRACEROUTE_UI_H
#define LORA_TRACEROUTE_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the LoRa traceroute screen — briefly pulses while "tracing" the
 *        mesh, then reveals the vertical hop path (Base Camp, Gateway-1,
 *        Relay-7, Trekker), each hop row annotated with its link SNR. BACK or
 *        LEFT returns to the LoRa chat hub.
 */
void ui_lora_traceroute_open(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_TRACEROUTE_UI_H

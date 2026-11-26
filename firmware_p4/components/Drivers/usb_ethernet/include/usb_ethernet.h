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

#ifndef USB_ETHERNET_H
#define USB_ETHERNET_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "usb_ethernet_ntb.h"
#ifdef __cplusplus
extern "C" {
#endif
// All APIs belong to one service worker; USB callbacks only enqueue data.
esp_err_t usb_ethernet_start(void);
esp_err_t usb_ethernet_poll(void);
esp_err_t usb_ethernet_stop(void);
bool usb_ethernet_link(uint8_t out_mac[6]);
bool usb_ethernet_identity(uint16_t *out_vid, uint16_t *out_pid);
size_t usb_ethernet_receive(uint8_t *out_frame);
esp_err_t usb_ethernet_transmit(const uint8_t *frame, size_t length);
#ifdef __cplusplus
}
#endif
#endif

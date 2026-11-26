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

#ifndef USB_ETHERNET_NTB_H
#define USB_ETHERNET_NTB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_ETHERNET_FRAME_MAX 1514
#define USB_ETHERNET_NTB_SIZE  2048
#define USB_ETHERNET_FRAME_MIN 14

typedef bool (*usb_ethernet_frame_cb_t)(const uint8_t *frame, size_t length, void *context);

// Validate the entire NTB before handing borrowed frames to the consumer.
bool usb_ethernet_ntb_decode(const uint8_t *data,
                             size_t length,
                             usb_ethernet_frame_cb_t callback,
                             void *context);
size_t usb_ethernet_ntb_encode(uint8_t *out,
                               size_t capacity,
                               const uint8_t *frame,
                               size_t length,
                               uint16_t sequence,
                               uint16_t divisor,
                               uint16_t remainder,
                               uint16_t alignment);
#ifdef __cplusplus
}
#endif
#endif

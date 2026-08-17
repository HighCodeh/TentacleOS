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

#ifndef TUSB_DESC_H
#define TUSB_DESC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"
#include "tinyusb.h"

// Composite device interfaces: HID (BadUSB) + CDC-ACM (companion host link).
// CDC uses two interfaces (comm + data), so the data interface is CDC+1.
#define TUSB_DESC_ITF_NUM_HID   0
#define TUSB_DESC_ITF_NUM_CDC   1 // comm; data interface = 2
#define TUSB_DESC_ITF_NUM_TOTAL 3

// Endpoint addresses
#define TUSB_DESC_EP_HID_IN    0x81
#define TUSB_DESC_EP_CDC_NOTIF 0x82
#define TUSB_DESC_EP_CDC_OUT   0x03
#define TUSB_DESC_EP_CDC_IN    0x83

/**
 * @brief Initialize the TinyUSB driver with HID composite descriptors.
 *
 * Installs the GPIO ISR service (required for ESP32-P4 High Speed USB),
 * configures device/configuration/HID report descriptors, and installs
 * the TinyUSB driver.
 *
 * Must be called before any HID report transmission.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_FAIL if the TinyUSB driver installation fails
 */
esp_err_t busb_init(void);

/**
 * @brief Configure the USB-C data mux (TS3USB221) and default it to the UART
 *        bridge.
 *
 * The single Type-C connector is muxed between the CP2105 USB-UART bridge and
 * the P4 native USB PHY. Call once at boot so the pin is owned and deterministic
 * (serial console / flashing available by default).
 */
void usb_mux_init(void);

/**
 * @brief Switch the USB-C data lines at runtime (no reset needed).
 *
 * @param native  true = P4 native USB (TinyUSB HID + CDC); false = CP2105
 *                USB-UART bridge. Selecting native brings the TinyUSB composite
 *                up first. Only one path is live at a time (shared connector).
 * @return ESP_OK, or the TinyUSB install error when switching to native.
 */
esp_err_t usb_mux_set_native(bool native);

/** @brief Whether the mux currently routes to the P4 native USB. */
bool usb_mux_is_native(void);

#ifdef __cplusplus
}
#endif

#endif // TUSB_DESC_H

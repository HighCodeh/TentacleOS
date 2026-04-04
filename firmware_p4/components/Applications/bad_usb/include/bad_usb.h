// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BAD_USB_H
#define BAD_USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Initialize the BadUSB module.
 *
 * Initializes the TinyUSB stack and registers the USB HID driver
 * into the HID HAL layer.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t bad_usb_init(void);

/**
 * @brief Deinitialize the BadUSB module.
 *
 * Unregisters the HID callbacks and uninstalls the TinyUSB driver.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t bad_usb_deinit(void);

/**
 * @brief Block until the USB host mounts the device.
 *
 * Polls tud_mounted() in a loop, then waits an additional 2 seconds
 * to allow the host OS to enumerate the device.
 */
void bad_usb_wait_for_connection(void);

#ifdef __cplusplus
}
#endif

#endif // BAD_USB_H

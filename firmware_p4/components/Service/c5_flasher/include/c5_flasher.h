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

#ifndef C5_FLASHER_H
#define C5_FLASHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Initialize UART and GPIO pins for C5 flashing.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t c5_flasher_init(void);

/**
 * @brief Put the C5 into bootloader mode via GPIO strapping.
 */
void c5_flasher_enter_bootloader(void);

/**
 * @brief Reset the C5 into normal operation mode.
 */
void c5_flasher_reset_normal(void);

/**
 * @brief Flash the C5 firmware over UART using ESP serial protocol.
 *
 * If bin_data is NULL and C5_FIRMWARE_EMBEDDED is defined, uses the
 * embedded binary linked at build time.
 *
 * @param bin_data  Pointer to firmware binary, or NULL to use embedded.
 * @param bin_size  Size of the binary in bytes.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size);

#ifdef __cplusplus
}
#endif

#endif // C5_FLASHER_H

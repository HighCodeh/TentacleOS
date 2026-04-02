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

#ifndef SUBGHZ_STORAGE_H
#define SUBGHZ_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "subghz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Sub-GHz storage subsystem.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_storage_init(void);

/**
 * @brief Save a decoded signal to persistent storage.
 *
 * @param name       User-assigned name for the capture.
 * @param data       Decoded signal data.
 * @param frequency  Center frequency in Hz.
 * @param te         Base time element in microseconds.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_storage_save_decoded(const char *name, const subghz_data_t *data, uint32_t frequency, uint32_t te);

/**
 * @brief Save raw pulse data to persistent storage.
 *
 * @param name       User-assigned name for the capture.
 * @param pulses     Array of signed pulse durations in microseconds.
 * @param count      Number of elements in the pulses array.
 * @param frequency  Center frequency in Hz.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_storage_save_raw(const char *name, const int32_t *pulses, size_t count, uint32_t frequency);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_STORAGE_H

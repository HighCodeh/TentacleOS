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

#ifndef SUBGHZ_TRANSMITTER_H
#define SUBGHZ_TRANSMITTER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Sub-GHz transmitter.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_tx_init(void);

/**
 * @brief Stop the Sub-GHz transmitter.
 */
void subghz_tx_stop(void);

/**
 * @brief Transmit a raw pulse sequence.
 *
 * @param timings  Array of signed pulse durations in microseconds
 *                 (positive = high, negative = low).
 * @param count    Number of elements in the timings array.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_tx_send_raw(const int32_t *timings, size_t count);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_TRANSMITTER_H

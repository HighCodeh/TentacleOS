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

#ifndef SUBGHZ_RECEIVER_H
#define SUBGHZ_RECEIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "cc1101.h"

/**
 * @brief Receiver operating mode.
 */
typedef enum {
  SUBGHZ_MODE_SCAN,   /**< @brief Scan for known protocols. */
  SUBGHZ_MODE_RAW,    /**< @brief Capture raw signal data. */
  SUBGHZ_MODE_COUNT   /**< @brief Number of receiver modes (sentinel). */
} subghz_mode_t;

/**
 * @brief Start the Sub-GHz receiver.
 *
 * @param mode    Operating mode (scan or raw).
 * @param preset  CC1101 radio preset configuration.
 * @param freq    Center frequency in Hz.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_receiver_start(subghz_mode_t mode, cc1101_preset_t preset, uint32_t freq);

/**
 * @brief Stop the Sub-GHz receiver.
 */
void subghz_receiver_stop(void);

/**
 * @brief Check whether the receiver is currently running.
 *
 * @return true if the receiver is active, false otherwise.
 */
bool subghz_receiver_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_RECEIVER_H

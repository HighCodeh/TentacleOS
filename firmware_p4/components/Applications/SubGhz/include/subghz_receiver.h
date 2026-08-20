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

#ifndef SUBGHZ_RECEIVER_H
#define SUBGHZ_RECEIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "cc1101.h"
#include "subghz_analyzer.h"
#include "subghz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Receiver operating mode.
 */
typedef enum {
  SUBGHZ_MODE_SCAN = 0, /**< @brief Scan for known protocols. */
  SUBGHZ_MODE_RAW,      /**< @brief Capture raw signal data. */
  SUBGHZ_MODE_COUNT     /**< @brief Number of receiver modes (sentinel). */
} subghz_mode_t;

/**
 * @brief Latest capture published by the receiver task for UI consumers.
 */
typedef struct {
  uint32_t seq;                      /**< @brief Capture counter; 0 means nothing captured yet. */
  bool decoded;                      /**< @brief true if a known protocol was decoded. */
  subghz_data_t data;                /**< @brief Decoded fields; protocol_name points to name_buf. */
  char name_buf[32];                 /**< @brief Backing storage for data.protocol_name. */
  char save_name[40];                /**< @brief File name the capture was auto-saved under. */
  uint32_t freq;                     /**< @brief Frequency in Hz at the moment of capture. */
  subghz_analyzer_result_t analysis; /**< @brief Timing/modulation analysis of the capture. */
} subghz_rx_result_t;

/**
 * @brief Copy the most recent capture published by the receiver task.
 *
 * @param out  Destination buffer.
 * @return true if a capture is available (out is filled), false otherwise.
 */
bool subghz_receiver_get_result(subghz_rx_result_t *out);

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

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

#ifndef ST25R3916_AAT_H
#define ST25R3916_AAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Result of an AAT calibration sweep.
 */
typedef struct {
  uint8_t dac_a;     /**< Best DAC A value. */
  uint8_t dac_b;     /**< Best DAC B value. */
  uint8_t amplitude; /**< Measured amplitude. */
  uint8_t phase;     /**< Measured phase. */
} st25r3916_aat_result_t;

/**
 * @brief Run a 2-pass antenna tuning sweep and apply best values.
 *
 * @param[out] out_result Receives the best calibration point.
 * V2.03 has a fixed matching network and unconnected AAT outputs: no sweep or
 * field activation is performed for that board.
 * @return ESP_OK on success; ESP_ERR_NOT_SUPPORTED for V2.03; an error otherwise.
 */
esp_err_t st25r3916_aat_calibrate(st25r3916_aat_result_t *out_result);

/**
 * @brief Load cached AAT calibration values from NVS.
 */
esp_err_t st25r3916_aat_load_nvs(st25r3916_aat_result_t *out_result);

/**
 * @brief Persist AAT calibration values to NVS.
 */
esp_err_t st25r3916_aat_save_nvs(const st25r3916_aat_result_t *result);

#ifdef __cplusplus
}
#endif

#endif // ST25R3916_AAT_H

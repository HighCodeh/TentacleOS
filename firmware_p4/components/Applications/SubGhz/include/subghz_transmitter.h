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

#ifndef SUBGHZ_TRANSMITTER_H
#define SUBGHZ_TRANSMITTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "cc1101.h"

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

typedef struct {
  bool is_busy;
  esp_err_t error;
} subghz_tx_status_t;

/** Pending jobs include the active frame; success requires is_busy=false and ESP_OK. */
void subghz_tx_get_status(subghz_tx_status_t *out_status);

/**
 * @brief Transmit a raw pulse sequence.
 *
 * @param timings  Array of signed pulse durations in microseconds
 *                 (positive = high, negative = low).
 * @param count    Number of elements in the timings array.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_tx_send_raw(const int32_t *timings, size_t count);
/* Configuration is copied into the queued job and applied by its radio worker. */
typedef struct {
  uint32_t frequency_hz;
  cc1101_preset_t preset;
} subghz_tx_config_t;
esp_err_t
subghz_tx_send_configured(const int32_t *timings, size_t count, const subghz_tx_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_TRANSMITTER_H

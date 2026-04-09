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

#ifndef CLIENT_SCANNER_H
#define CLIENT_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Record for a discovered client station.
 */
typedef struct {
  uint8_t bssid[6];
  uint8_t client_mac[6];
  int8_t rssi;
  uint8_t channel;
} client_scanner_record_t;

/**
 * @brief Start a client scan over SPI.
 *
 * @return true on success, false on failure.
 */
bool client_scanner_start(void);

/**
 * @brief Get cached client scan results.
 *
 * @param[out] out_count  Number of results.
 * @return Pointer to result array, or NULL if scan not ready.
 */
client_scanner_record_t *client_scanner_get_results(uint16_t *out_count);

/**
 * @brief Free cached client scan results.
 */
void client_scanner_free_results(void);

/**
 * @brief Save client scan results to internal flash via SPI.
 *
 * @return true on success, false on failure.
 */
bool client_scanner_save_results_to_internal_flash(void);

/**
 * @brief Save client scan results to SD card via SPI.
 *
 * @return true on success, false on failure.
 */
bool client_scanner_save_results_to_sd_card(void);

#ifdef __cplusplus
}
#endif

#endif // CLIENT_SCANNER_H

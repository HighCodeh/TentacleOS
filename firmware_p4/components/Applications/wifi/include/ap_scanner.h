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

#ifndef AP_SCANNER_H
#define AP_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_wifi_types.h"

/**
 * @brief Start an AP scan over SPI and cache results.
 *
 * @return true on success, false on SPI failure.
 */
bool ap_scanner_start(void);

/**
 * @brief Get cached AP scan results.
 *
 * @param[out] out_count  Number of results. Must not be NULL.
 * @return Pointer to result array, or NULL if scan not ready.
 */
wifi_ap_record_t *ap_scanner_get_results(uint16_t *out_count);

/**
 * @brief Free cached AP scan results.
 */
void ap_scanner_free_results(void);

/**
 * @brief Save AP scan results to internal flash via SPI.
 *
 * @return true on success, false on failure.
 */
bool ap_scanner_save_results_to_internal_flash(void);

/**
 * @brief Save AP scan results to SD card via SPI.
 *
 * @return true on success, false on failure.
 */
bool ap_scanner_save_results_to_sd_card(void);
/* Borrowed immutable snapshot; persists to SD only. */
bool ap_scanner_save_snapshot(const wifi_ap_record_t *records, uint16_t count);

#ifdef __cplusplus
}
#endif

#endif // AP_SCANNER_H

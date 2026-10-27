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

#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "bluetooth_service.h"

/**
 * @brief Start a BLE scan task.
 *
 * @return true if scan started, false on failure.
 */
bool ble_scanner_start(void);

/** @brief Return true while the C5 scan worker is still running. */
bool ble_scanner_is_scanning(void);

/**
 * @brief Get the scan results.
 *
 * @param[out] out_count Pointer to store the number of results.
 * @return Pointer to the scan results array, or NULL if scanning or empty.
 */
bluetooth_service_scan_result_t *ble_scanner_get_results(uint16_t *out_count);

/**
 * @brief Free the scan results and release associated memory.
 */
void ble_scanner_free_results(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_SCANNER_H

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

#ifndef BLE_SNIFFER_H
#define BLE_SNIFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief One sniffed advertisement passed to the observer.
 *
 * @p addr/@p data point at buffers owned by the sniffer, valid only for the
 * duration of the observer call.
 */
typedef struct {
  const uint8_t *addr;
  uint8_t addr_type;
  int rssi;
  const uint8_t *data;
  uint8_t len;
} ble_sniffer_adv_t;

/**
 * @brief Observer invoked for every sniffed advertisement.
 *
 * Runs in the sniffer task context (radio core), NOT the LVGL thread - a UI
 * observer must only copy the data into its own lock-guarded buffer and never
 * touch LVGL directly.
 */
typedef void (*ble_sniffer_observer_t)(const ble_sniffer_adv_t *adv);

/**
 * @brief Register (or clear, with NULL) an observer for live sniffed frames.
 *        Independent of start/stop; safe to set before or after starting.
 */
void ble_sniffer_set_observer(ble_sniffer_observer_t cb);

/**
 * @brief Start the BLE packet sniffer.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t ble_sniffer_start(void);

/**
 * @brief Stop the BLE packet sniffer and free resources.
 */
void ble_sniffer_stop(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_SNIFFER_H

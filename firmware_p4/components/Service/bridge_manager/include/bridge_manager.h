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

#ifndef BRIDGE_MANAGER_H
#define BRIDGE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Synchronize P4 and C5 firmware versions and initialize the bridge.
 *
 * Queries the C5 firmware version over SPI. If it differs from the
 * expected version, reports it without automatic OTA. Starts bounded recovery.
 *
 * @return ESP_OK on success, ESP_FAIL if synchronization fails.
 */
esp_err_t bridge_manager_init(void);

/**
 * @brief Manually trigger a C5 firmware update.
 *
 * @return ESP_OK on success, or an error code from c5_flasher.
 */
esp_err_t bridge_manager_force_update(void);

#ifdef __cplusplus
}
#endif

#endif // BRIDGE_MANAGER_H

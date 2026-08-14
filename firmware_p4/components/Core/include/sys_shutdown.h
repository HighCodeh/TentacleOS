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

#ifndef SYS_SHUTDOWN_H
#define SYS_SHUTDOWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Register the graceful shutdown handler.
 *
 * Installs a handler that runs on every esp_restart() and clean shutdown,
 * unmounting storage so in-flight config writes are flushed before the restart.
 * Call once during boot.
 *
 * @return
 *   - ESP_OK on success
 *   - error code from esp_register_shutdown_handler on failure
 */
esp_err_t sys_shutdown_init(void);

#ifdef __cplusplus
}
#endif

#endif // SYS_SHUTDOWN_H

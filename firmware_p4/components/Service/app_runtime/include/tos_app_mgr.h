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

// Foreground app lifecycle. v1 runs one app at a time in its own supervised
// task, with a cooperative stop (the app polls api->should_stop). The manager
// owns a copy of the bundle for the app's lifetime.

#ifndef TOS_APP_MGR_H
#define TOS_APP_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "tos_api.h"

#define TOS_APP_MAX      4  // apps that may run concurrently
#define TOS_APP_NAME_CAP 16

/**
 * @brief Start a .hb app in its own task. Copies @p hb, so the caller may free
 *        its buffer on return. ESP_ERR_NO_MEM if all app slots are in use.
 */
esp_err_t tos_app_mgr_start(const uint8_t *hb, size_t len, const tos_api_t *api);

/**
 * @brief Ask the app named @p name to stop and wait up to @p timeout_ms for a
 *        cooperative exit, then force-kill it. ESP_ERR_NOT_FOUND if no such app.
 */
esp_err_t tos_app_mgr_stop(const char *name, uint32_t timeout_ms);

/**
 * @brief Copy the names of running apps into @p names (up to @p max).
 * @return the number of apps running.
 */
int tos_app_mgr_list(char names[][TOS_APP_NAME_CAP], int max);

#ifdef __cplusplus
}
#endif

#endif // TOS_APP_MGR_H

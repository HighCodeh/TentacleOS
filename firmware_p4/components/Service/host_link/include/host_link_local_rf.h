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

#ifndef HOST_LINK_LOCAL_RF_H
#define HOST_LINK_LOCAL_RF_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "host_link_rf.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t host_local_rf_init(void);
bool host_local_rf_is_ready(void);
bool host_local_rf_lock(uint32_t timeout_ms);
void host_local_rf_unlock(void);
/** Caller holds the local RF lock until this synchronous call returns. */
esp_err_t host_local_rf_execute(host_rf_request_t *request);
typedef esp_err_t (*host_local_rf_cb_t)(void *context);
/** Firmware callback only. Caller holds the kill gate until the worker returns. */
esp_err_t host_local_rf_call(host_local_rf_cb_t callback, void *context);
/** Host core worker only; queued local work never emits a transport response. */
void host_local_rf_process(void);
/** Queue a trusted local RF operation; used only by the internal broker. */
esp_err_t host_link_schedule_local_rf(void);

#ifdef __cplusplus
}
#endif
#endif // HOST_LINK_LOCAL_RF_H

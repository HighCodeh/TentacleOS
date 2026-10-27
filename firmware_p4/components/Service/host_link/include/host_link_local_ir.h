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

#ifndef HOST_LINK_LOCAL_IR_H
#define HOST_LINK_LOCAL_IR_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "host_link_rf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_LOCAL_IR_CODE_SIZE      10
#define HOST_LOCAL_IR_TIMEOUT_MAX_MS 1000
bool host_local_ir_is_command(uint16_t command);
/* Persistent host worker only, behind the local-call kill gate. */
esp_err_t host_local_ir_execute(host_rf_request_t *request);

#ifdef __cplusplus
}
#endif
#endif // HOST_LINK_LOCAL_IR_H

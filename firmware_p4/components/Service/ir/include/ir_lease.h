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

#ifndef IR_LEASE_H
#define IR_LEASE_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal persistent-worker lease. Native IR calls are rejected while held. */
esp_err_t ir_lease_acquire(void);
esp_err_t ir_lease_release(void);
bool ir_lease_enter(void);
void ir_lease_leave(bool is_active);

#ifdef __cplusplus
}
#endif
#endif // IR_LEASE_H

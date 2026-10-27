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

#ifndef CC1101_LEASE_H
#define CC1101_LEASE_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Persistent worker only. Release after RMT, buffers and radio are quiescent. */
esp_err_t cc1101_lease_acquire(void);
esp_err_t cc1101_lease_release(void);
/** Retain failed cleanup but reject unrelated commands on the same worker. */
void cc1101_lease_pause(void);
bool cc1101_lease_enter(void);
void cc1101_lease_leave(void);
void cc1101_lease_record_error(esp_err_t error);
esp_err_t cc1101_lease_take_error(void);

#ifdef __cplusplus
}
#endif
#endif // CC1101_LEASE_H

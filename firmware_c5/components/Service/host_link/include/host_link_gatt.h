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

#ifndef HOST_LINK_GATT_H
#define HOST_LINK_GATT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Companion host-link GATT server on the C5 (NimBLE). A NUS-style service with
// a write characteristic (app→device) and a notify characteristic
// (device→app). Opaque byte relay — all crypto/auth is on the P4. Mirrors
// meshcore_gatt, but uses "just works" LE Secure Connections (no MITM) since
// the host-link PSK/HMAC envelope is the real trust boundary.

/** @brief Start the GATT server and begin advertising as "<name_prefix>-XXXX". */
esp_err_t host_link_gatt_init(const char *name_prefix);

/** @brief Stop advertising / GATT and tear down the NimBLE host. */
void host_link_gatt_stop(void);

/** @brief True while the GATT server is running. */
bool host_link_gatt_is_running(void);

/** @brief True while a companion is connected. */
bool host_link_gatt_is_connected(void);

/** @brief True while the companion has enabled notifications on the TX char. */
bool host_link_gatt_is_subscribed(void);

/** @brief Notify the connected companion with a reassembled device→app frame. */
void host_link_gatt_notify(const uint8_t *frame, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_GATT_H

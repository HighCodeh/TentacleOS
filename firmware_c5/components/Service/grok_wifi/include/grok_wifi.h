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

#ifndef GROK_WIFI_H
#define GROK_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "spi_protocol.h"

// No work is performed at boot. START is explicit and never enables Wi-Fi.
// The caller must poll STATUS (or transfer data) at least every 10 seconds.
#ifdef __cplusplus
extern "C" {
#endif

bool grok_wifi_is_active(void);
void grok_wifi_stop(void);
spi_status_t grok_wifi_execute(
    spi_id_t id, const uint8_t *payload, uint8_t len, uint8_t *response, uint8_t *response_len);

#ifdef __cplusplus
}
#endif

#endif

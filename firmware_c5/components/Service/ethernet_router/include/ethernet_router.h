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

#ifndef ETHERNET_ROUTER_H
#define ETHERNET_ROUTER_H
#include <stdbool.h>
#include "spi_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
bool ethernet_router_is_active(void);
// Output capacity must be SPI_ETHERNET_REPLY_META_SIZE + SPI_ETHERNET_MTU.
spi_status_t ethernet_router_exchange(const uint8_t *payload,
                                      uint16_t length,
                                      uint8_t *out_payload,
                                      uint16_t *out_length);
spi_status_t ethernet_router_execute(
    spi_id_t id, const uint8_t *payload, uint8_t length, uint8_t *out, uint8_t *out_length);
#ifdef __cplusplus
}
#endif
#endif

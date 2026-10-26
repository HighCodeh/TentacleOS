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

/* Local SPI service only. No UART/host-link/network control surface. */
#ifndef HB_MUSE_C5_SERVICE_H
#define HB_MUSE_C5_SERVICE_H
#include "spi_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif

void hb_muse_c5_service_register(void);
spi_status_t hb_muse_c5_service_execute(
    uint16_t command, const uint8_t *input, uint8_t bytes, uint8_t *output, uint8_t *written);
void hb_muse_c5_service_revoke(void);
bool hb_muse_c5_service_allows(uint16_t command);
#ifdef __cplusplus
}
#endif

#endif

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

#ifndef WARDRIVE_RADIO_H
#define WARDRIVE_RADIO_H
#include "spi_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
spi_status_t wardrive_radio_command(
    spi_id_t id, const uint8_t *payload, uint8_t len, uint8_t *out_payload, uint8_t *out_len);
#ifdef __cplusplus
}
#endif
#endif

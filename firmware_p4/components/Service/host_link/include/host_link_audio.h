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

#ifndef HOST_LINK_AUDIO_H
#define HOST_LINK_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief True if @p cmd is an audio host-link command (category SPI_CAT_AUDIO). */
bool host_audio_is_op(uint16_t cmd);

/** @brief Handle an audio host-link command locally on the P4. Returns spi_status_t. */
uint8_t host_audio_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out,
                          uint16_t out_cap,
                          uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_AUDIO_H

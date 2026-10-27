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

#ifndef HOST_LINK_THEME_H
#define HOST_LINK_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// P4-local companion op that switches the active UI theme by name
// (SPI_ID_SYSTEM_SET_THEME). The theme apply is marshalled onto the LVGL thread.

/**
 * @brief Apply and persist a theme by name.
 *
 * @param cmd      Packed command id (SPI_ID_SYSTEM_SET_THEME).
 * @param payload  Theme name (not null-terminated).
 * @param plen     Payload length.
 * @param out_data Response data buffer (unused; no data returned).
 * @param out_cap  Capacity of @p out_data.
 * @param out_len  Receives the response data length (0).
 * @return spi_status_t value.
 */
uint8_t host_theme_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out_data,
                          uint16_t out_cap,
                          uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_THEME_H

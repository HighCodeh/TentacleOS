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

#ifndef HOST_LINK_CONFIG_H
#define HOST_LINK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// P4-local structured device settings for the companion (SPI_ID_SYSTEM_CONFIG_*).
// GET returns one section's packed struct; SET writes it into the matching
// tos_config global, persists it, and applies the change live (brightness,
// rotation, volume, wifi/ble enable, led). All local — never relayed to the C5.

/** @brief True if @p cmd is a companion settings op (CONFIG_GET / CONFIG_SET). */
bool host_config_is_op(uint16_t cmd);

/**
 * @brief Execute a companion settings op locally.
 *
 * @param cmd      Packed command id (SPI_ID_SYSTEM_CONFIG_GET / _SET).
 * @param payload  Byte 0 = spi_config_section_t; for SET, the section struct follows.
 * @param plen     Payload length.
 * @param out_data Response data buffer (the section struct on GET).
 * @param out_cap  Capacity of @p out_data.
 * @param out_len  Receives the response data length.
 * @return spi_status_t value.
 */
uint8_t host_config_handle(uint16_t cmd,
                           const uint8_t *payload,
                           uint16_t plen,
                           uint8_t *out_data,
                           uint16_t out_cap,
                           uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_CONFIG_H

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

#ifndef HOST_LINK_DISPATCH_H
#define HOST_LINK_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Handles one host-link command range. Returns spi_status_t. */
typedef uint8_t (*host_link_handler_fn)(uint16_t cmd,
                                        const uint8_t *payload,
                                        uint16_t plen,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len);

/**
 * @brief One entry in the host-link dispatch table.
 *
 * A command matches when its category equals @ref category and its op falls in
 * [op_min, op_max]. Use op_min=0x00, op_max=0xFF to claim a whole category, or
 * op_min==op_max for a single op. Commands that match no entry relay to the C5.
 */
typedef struct {
  uint8_t              category; ///< SPI_CAT_*, matched on (cmd >> 8)
  uint8_t              op_min;   ///< inclusive op range within the category
  uint8_t              op_max;   ///< 0x00..0xFF spans the whole category
  uint16_t             out_cap;  ///< response bytes this handler may write
  host_link_handler_fn handle;   ///< handler; returns spi_status_t
  const char          *name;     ///< short label for logs
} host_link_handler_t;

/**
 * @brief Register a handler. Fails if the (category, op_min) pair is taken or the
 *        table is full. Safe to call at runtime (e.g. when an app loads).
 */
esp_err_t host_link_register(const host_link_handler_t *h);

/** @brief Remove the handler previously registered with this (category, op_min). */
esp_err_t host_link_unregister(uint8_t category, uint8_t op_min);

/**
 * @brief Copy the handler matching @p cmd into @p out_copy.
 * @return true if a handler matched. The copy shields the caller from a
 *         concurrent unregister while it invokes the handler.
 */
bool host_link_dispatch_lookup(uint16_t cmd, host_link_handler_t *out_copy);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_DISPATCH_H

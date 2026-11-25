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

#ifndef SUBGHZ_TYPES_H
#define SUBGHZ_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decoded Sub-GHz signal data.
 */
typedef struct {
  const char *protocol_name; /**< @brief Name of the decoded protocol. */
  uint32_t serial;           /**< @brief Device serial number. */
  uint8_t btn;               /**< @brief Button code. */
  uint8_t bit_count;         /**< @brief Number of bits in the raw value. */
  uint32_t raw_value;        /**< @brief Raw decoded value. */
  uint32_t te_us;            /**< @brief Measured base interval; zero uses protocol default. */
  uint32_t guard_time;       /**< @brief Princeton low guard in TE units; zero uses default. */
} subghz_data_t;

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_TYPES_H

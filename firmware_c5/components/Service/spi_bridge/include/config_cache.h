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

#ifndef CONFIG_CACHE_H
#define CONFIG_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

// C5 side of the P4->C5 config cache. The P4 (master) pushes config files when
// their CRC32 differs from the hash the C5 has stored; the C5 writes them to its
// own assets partition and records the new hash in NVS. See spi_cfg_id_t.

/** @brief Stored hash for @p id (0 if none/unknown). */
uint32_t config_cache_get_hash(uint8_t id);

/** @brief Begin receiving config @p id of @p size bytes, tagged with @p hash. */
esp_err_t config_cache_begin(uint8_t id, uint32_t size, uint32_t hash);

/** @brief Append a chunk at @p offset into the staging buffer. */
esp_err_t config_cache_chunk(uint8_t id, uint32_t offset, const uint8_t *data, uint16_t len);

/** @brief Write the staged config to flash and persist its hash. */
esp_err_t config_cache_commit(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_CACHE_H

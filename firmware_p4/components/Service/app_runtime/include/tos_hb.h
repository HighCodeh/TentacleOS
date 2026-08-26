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

// Opener for the .hb app bundle (HighBoy App). The container carries a small
// fixed header then the raw ELF. Signature verification (Phase 5) is not here
// yet; for now this only parses and bounds-checks.

#ifndef TOS_HB_H
#define TOS_HB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  uint16_t abi_major;
  uint16_t abi_minor;
  uint32_t caps; ///< tos_cap_t bitmask the app requests
  char name[16]; ///< null-terminated app name
  const uint8_t *elf;
  size_t elf_len;
} tos_hb_t;

/**
 * @brief Parse and bounds-check a .hb bundle in @p buf.
 * @return ESP_OK and fills @p out (elf points into buf, not a copy). Fails on a
 *         bad magic, unsupported format version, or a length that runs past buf.
 */
esp_err_t tos_hb_open(const uint8_t *buf, size_t len, tos_hb_t *out);

#ifdef __cplusplus
}
#endif

#endif // TOS_HB_H

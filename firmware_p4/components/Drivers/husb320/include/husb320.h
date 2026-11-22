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

#ifndef HUSB320_H
#define HUSB320_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool is_attached;
  uint8_t last_reg;       // Last accessed register on failure, zero on success/no transfer.
  uint16_t advertised_ma; // Zero means default USB current, not a negotiated budget.
} husb320_source_t;

// System-lifetime device; boot and the USB power worker serialize initialization.
esp_err_t husb320_init(void);
esp_err_t husb320_read_source(husb320_source_t *out_source);

#ifdef __cplusplus
}
#endif
#endif

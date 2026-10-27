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

#ifndef GATT_EXPLORER_H
#define GATT_EXPLORER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type);
bool gatt_explorer_is_busy(void);
esp_err_t gatt_explorer_get_status(spi_gatt_status_t *out);
esp_err_t gatt_explorer_get_record(uint8_t index, spi_gatt_record_t *out);
esp_err_t gatt_explorer_read(uint16_t handle);
esp_err_t gatt_explorer_stop(void);
/* Called only after the NimBLE host has stopped, before its port is deinitialized. */
esp_err_t gatt_explorer_deinit(void);

#ifdef __cplusplus
}
#endif
#endif // GATT_EXPLORER_H

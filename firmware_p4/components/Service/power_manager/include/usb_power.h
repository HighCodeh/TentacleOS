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

#ifndef USB_POWER_H
#define USB_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool is_source_valid;
  bool is_attached;
  uint16_t advertised_ma;
  uint16_t applied_ma;
  esp_err_t source_error;
  uint8_t source_reg;
  esp_err_t input_error;
} usb_power_snapshot_t;

// System-lifetime worker; initialize once from boot after the charger and LEDs.
esp_err_t usb_power_init(void);
bool usb_power_get(usb_power_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
#endif

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

#ifndef RF_LAB_HW_H
#define RF_LAB_HW_H
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  esp_err_t c5, cc, sx, nfc, lf;
} rf_lab_hw_status_t;
/* Boot-only: no acquisition tasks or radio owners may exist. */
void rf_lab_hw_quiet(rf_lab_hw_status_t *out_status);
esp_err_t rf_lab_hw_path_set(uint8_t path);
esp_err_t rf_lab_hw_path_get(uint8_t *out_path);
#ifdef __cplusplus
}
#endif
#endif

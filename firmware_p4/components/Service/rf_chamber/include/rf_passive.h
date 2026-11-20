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

#ifndef RF_PASSIVE_H
#define RF_PASSIVE_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rf_lab_hw.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  rf_lab_hw_status_t hardware;
  esp_err_t error;
  uint8_t path;
  uint8_t actual_path;
} rf_passive_status_t;
esp_err_t rf_passive_load(void);
bool rf_passive_is_boot(void);
void rf_passive_startup(void);
esp_err_t rf_passive_save_boot(bool enabled, uint8_t path);
esp_err_t rf_passive_select(uint8_t path);
void rf_passive_snapshot(rf_passive_status_t *out_status);
#ifdef __cplusplus
}
#endif
#endif

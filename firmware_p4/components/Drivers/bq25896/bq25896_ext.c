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

#include "bq25896.h"

bool bq25896_get_charge_enable(void) {
  return true;
}

esp_err_t bq25896_set_charge_enable(bool enable) {
  (void)enable;
  return ESP_OK;
}

esp_err_t bq25896_power_off(void) {
  return ESP_OK;
}

uint8_t bq25896_reg_raw(uint8_t reg) {
  (void)reg;
  return 0;
}

esp_err_t bq25896_read_telemetry(bq25896_telem_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;

  uint16_t vbat = bq25896_get_battery_voltage();
  bq25896_vbus_status_t vbus = bq25896_get_vbus_status();

  out->vbat_mv = vbat;
  out->soc = bq25896_get_battery_percentage(vbat);
  out->chg = bq25896_get_charge_status();
  out->vbus = vbus;
  out->charging = bq25896_is_charging();
  out->power_good = (vbus != VBUS_STATUS_UNKNOWN);

  out->vsys_mv = 0;
  out->vbus_mv = 0;
  out->ichg_ma = 0;
  out->iinlim_ma = 0;
  out->fault = 0;

  return ESP_OK;
}

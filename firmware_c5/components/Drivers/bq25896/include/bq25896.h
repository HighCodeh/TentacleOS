// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BQ25896_H
#define BQ25896_H

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/i2c.h"
#include <stdint.h>
#include <stdbool.h>

#define BQ25896_I2C_ADDR 0x6B

typedef enum {
  CHARGE_STATUS_NOT_CHARGING = 0,
  CHARGE_STATUS_PRECHARGE = 1,
  CHARGE_STATUS_FAST_CHARGE = 2,
  CHARGE_STATUS_CHARGE_DONE = 3
} bq25896_charge_status_t;

typedef enum {
  VBUS_STATUS_UNKNOWN = 0,
  VBUS_STATUS_USB_HOST = 1,
  VBUS_STATUS_ADAPTER_PORT = 2,
  VBUS_STATUS_OTG = 3
} bq25896_vbus_status_t;

/**
 * @brief Initialize the BQ25896 charger IC.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_FAIL on communication error
 */
esp_err_t bq25896_init(void);

/**
 * @brief Get the current charge status.
 *
 * @return Charge status enum value.
 */
bq25896_charge_status_t bq25896_get_charge_status(void);

/**
 * @brief Get the VBUS status (charger connection state).
 *
 * @return VBUS status enum value.
 */
bq25896_vbus_status_t bq25896_get_vbus_status(void);

/**
 * @brief Get the battery voltage in millivolts.
 *
 * @return Battery voltage in mV, or 0 on read error.
 */
uint16_t bq25896_get_battery_voltage(void);

/**
 * @brief Check if the battery is currently charging.
 *
 * @return true if pre-charging or fast-charging.
 */
bool bq25896_is_charging(void);

/**
 * @brief Estimate battery percentage from voltage.
 *
 * @param voltage_mv Battery voltage in millivolts.
 * @return Estimated percentage (0-100).
 */
int bq25896_get_battery_percentage(uint16_t voltage_mv);

#ifdef __cplusplus
}
#endif

#endif // BQ25896_H

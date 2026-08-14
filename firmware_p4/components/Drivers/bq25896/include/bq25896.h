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

#ifndef BQ25896_H
#define BQ25896_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "driver/i2c.h"
#include "esp_err.h"

#define BQ25896_I2C_ADDR 0x6B

/** @brief Battery charge state reported by the charger. */
typedef enum {
  CHARGE_STATUS_NOT_CHARGING = 0, ///< Not charging.
  CHARGE_STATUS_PRECHARGE = 1,    ///< Pre-charge phase.
  CHARGE_STATUS_FAST_CHARGE = 2,  ///< Fast-charge phase.
  CHARGE_STATUS_CHARGE_DONE = 3   ///< Charge complete.
} bq25896_charge_status_t;

/** @brief VBUS (charger input) connection state. */
typedef enum {
  VBUS_STATUS_UNKNOWN = 0,      ///< No/unknown VBUS source.
  VBUS_STATUS_USB_HOST = 1,     ///< USB host (SDP) input.
  VBUS_STATUS_ADAPTER_PORT = 2, ///< Dedicated adapter input.
  VBUS_STATUS_OTG = 3           ///< OTG (boost) output.
} bq25896_vbus_status_t;

/**
 * @brief Initialize the BQ25896 charger IC.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t bq25896_init(void);

/** @brief Whether the charger answered on I2C at init (chip present on the bus). */
bool bq25896_is_present(void);

/** @brief Read the raw fault register (REG0C): CHRG_FAULT[5:4], BAT_FAULT[3], NTC_FAULT[2:0]. */
uint8_t bq25896_get_fault(void);

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
 * @return Battery voltage in mV, or 0 on read failure.
 */
uint16_t bq25896_get_battery_voltage(void);

/**
 * @brief Check if the battery is currently charging.
 *
 * @return true if pre-charging or fast charging, false otherwise.
 */
bool bq25896_is_charging(void);

/**
 * @brief Estimate battery percentage from voltage.
 *
 * @param voltage_mv  Battery voltage in millivolts.
 * @return Estimated percentage (0-100).
 */
int bq25896_get_battery_percentage(uint16_t voltage_mv);

// --- Extended telemetry + control ------------------------------------------
// Additive layer used by the power screen. Implemented in bq25896_ext.c on top
// of the public base API above: battery voltage/percent/charge/vbus are real,
// and the charge-enable / ship-mode / raw-register controls are now real
// register writes (bq25896.c). Only the aggregated telemetry's VSYS/VBUS mV and
// currents remain approximate.

/** @brief Whether battery charging is currently enabled (REG03 CHG_CONFIG). */
bool bq25896_get_charge_enable(void);

/**
 * @brief Enable/disable battery charging (CE pin + REG03 CHG_CONFIG).
 *
 * @param enable  true to enable charging, false to disable.
 * @return ESP_OK on success, otherwise an esp_err_t error code.
 */
esp_err_t bq25896_set_charge_enable(bool enable);

/**
 * @brief Power the system OFF via BATFET_DIS / ship mode (REG09 bit5).
 *
 * Real power-off: disconnects the battery. Has no effect while VBUS (USB) is
 * present - the part keeps the system powered from USB in that case.
 *
 * @return ESP_OK on success, otherwise an esp_err_t error code.
 */
esp_err_t bq25896_power_off(void);

/** @brief One-shot snapshot of the charger/battery state for a UI. */
typedef struct {
  uint16_t vbat_mv;            ///< Battery voltage in mV.
  uint16_t vsys_mv;            ///< System voltage in mV.
  uint16_t vbus_mv;            ///< VBUS voltage in mV.
  uint16_t ichg_ma;            ///< Charge current in mA.
  uint16_t iinlim_ma;          ///< Input current limit in mA.
  uint8_t fault;               ///< Raw fault register value.
  int soc;                     ///< Estimated state of charge (0-100).
  bq25896_charge_status_t chg; ///< Current charge status.
  bq25896_vbus_status_t vbus;  ///< Current VBUS status.
  bool charging;               ///< True while pre/fast charging.
  bool power_good;             ///< True when a valid VBUS source is present.
} bq25896_telem_t;

/**
 * @brief Fill @p out with a telemetry snapshot (real battery data, mocked diags).
 *
 * @param[out] out  Destination snapshot. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if @p out is NULL.
 */
esp_err_t bq25896_read_telemetry(bq25896_telem_t *out);

/**
 * @brief Raw read of any register.
 *
 * @param reg  Register address to read.
 * @return Register value, or 0 in this mocked build.
 */
uint8_t bq25896_reg_raw(uint8_t reg);

#ifdef __cplusplus
}
#endif

#endif // BQ25896_H

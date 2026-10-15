// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef BQ25896_H
#define BQ25896_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* High Boy V2 schematic U4: BQ25896RTWR, 7-bit I2C address 0x6B. */
#define BQ25896_I2C_ADDR       0x6B
#define BQ25896_REGISTER_COUNT 0x15

/** Battery charge state reported by REG0B.CHRG_STAT. */
typedef enum {
  CHARGE_STATUS_NOT_CHARGING = 0,
  CHARGE_STATUS_PRECHARGE = 1,
  CHARGE_STATUS_FAST_CHARGE = 2,
  CHARGE_STATUS_CHARGE_DONE = 3,
} bq25896_charge_status_t;

/** Input-source state reported by REG0B.VBUS_STAT. Values are silicon codes. */
typedef enum {
  VBUS_STATUS_UNKNOWN = 0,
  VBUS_STATUS_USB_HOST = 1,
  VBUS_STATUS_ADAPTER_PORT = 2,
  VBUS_STATUS_OTG = 7,
} bq25896_vbus_status_t;

typedef enum {
  BQ25896_CHARGE_FAULT_NONE = 0,
  BQ25896_CHARGE_FAULT_INPUT = 1,
  BQ25896_CHARGE_FAULT_THERMAL = 2,
  BQ25896_CHARGE_FAULT_TIMER = 3,
} bq25896_charge_fault_t;

typedef enum {
  BQ25896_NTC_NORMAL = 0,
  BQ25896_NTC_WARM = 2,
  BQ25896_NTC_COOL = 3,
  BQ25896_NTC_COLD = 5,
  BQ25896_NTC_HOT = 6,
} bq25896_ntc_state_t;

/** Complete, internally consistent snapshot of the charger registers. */
typedef struct {
  /* ADC measurements. */
  uint16_t vbat_mv;
  uint16_t vsys_mv;
  uint16_t vbus_mv;
  uint16_t ichg_ma;
  uint32_t ts_milli_pct;     ///< TS voltage as 1/1000 percent of REGN (e.g. 50000 =
                             ///< 50%).
  int16_t temperature_decic; ///< Board NTC estimate in 0.1 C (about +/-5 C).

  /* Programmed/effective limits. */
  uint16_t input_current_limit_ma;
  uint16_t effective_input_limit_ma;
  uint16_t charge_current_limit_ma;
  uint16_t precharge_current_limit_ma;
  uint16_t termination_current_limit_ma;
  uint16_t charge_voltage_limit_mv;
  uint16_t input_voltage_limit_mv;
  uint16_t boost_voltage_mv;
  uint16_t boost_current_limit_ma;
  uint16_t system_min_voltage_mv;

  /* Identity and raw state, useful for field diagnostics. */
  uint8_t part_number;
  uint8_t device_revision;
  uint8_t raw_status;
  uint8_t fault_latched; ///< First REG0C read: pre-existing/latched fault.
  uint8_t fault_live;    ///< Immediate second REG0C read: current fault.
  uint8_t fault;         ///< Compatibility alias: fault_latched | fault_live.
  uint8_t raw_part_info;
  uint8_t soc; ///< Voltage-only estimate; not a replacement for the BQ27220 gauge.

  bq25896_charge_status_t chg;
  bq25896_vbus_status_t vbus;
  bq25896_charge_fault_t charge_fault;
  bq25896_ntc_state_t ntc_state;

  bool charging;
  bool charge_enabled;
  bool otg_enabled;
  bool hiz_enabled;
  bool power_good; ///< Valid external input; deliberately false while sourcing
                   ///< OTG.
  bool vbus_good;
  bool adc_continuous;
  bool ico_enabled;
  bool ico_optimized;
  bool thermal_regulation;
  bool vsys_regulation;
  bool input_current_dpm;
  bool input_voltage_dpm;
  bool watchdog_fault;
  bool boost_fault;
  bool battery_ovp;
} bq25896_telem_t;

/** Initialize and place the BQ25896 in a deterministic, hardware-safe state. */
esp_err_t bq25896_init(void);

/** Whether U4 answered and identified itself as BQ25896 (REG14.PN == 000). */
bool bq25896_is_present(void);

/** Reapply runtime-only settings after a bus/chip disturbance, without a
 * register reset. */
esp_err_t bq25896_reapply_runtime_config(void);

/** Read one register with error reporting. */
esp_err_t bq25896_read_register(uint8_t reg, uint8_t *value);

/** Read REG00..REG14. REG0C is read twice as required by the datasheet. */
esp_err_t bq25896_dump_registers(uint8_t regs[BQ25896_REGISTER_COUNT]);

/** Capture all useful ADC, status, limit, DPM, identity and fault fields. */
esp_err_t bq25896_read_telemetry(bq25896_telem_t *out);

uint8_t bq25896_get_fault(void);
bq25896_charge_status_t bq25896_get_charge_status(void);
bq25896_vbus_status_t bq25896_get_vbus_status(void);
uint16_t bq25896_get_battery_voltage(void);
bool bq25896_is_charging(void);
int bq25896_get_battery_percentage(uint16_t voltage_mv);

/** Charging is gated by REG03 only; High Boy CE is physically tied low. */
bool bq25896_get_charge_enable(void);
esp_err_t bq25896_set_charge_enable(bool enable);

/** Safe setters return the quantized value actually programmed when requested.
 */
esp_err_t bq25896_set_input_current_limit(uint16_t requested_ma, uint16_t *actual_ma);
esp_err_t bq25896_set_charge_current_limit(uint16_t requested_ma, uint16_t *actual_ma);
esp_err_t bq25896_set_charge_voltage_limit(uint16_t requested_mv, uint16_t *actual_mv);

/** Configure and control the 5-V boost path. High Boy GPIO33 is the OTG gate.
 */
esp_err_t bq25896_set_otg_profile(uint16_t requested_mv,
                                  uint16_t requested_ma,
                                  uint16_t *actual_mv,
                                  uint16_t *actual_ma);
esp_err_t bq25896_set_otg_enable(bool enable);
bool bq25896_get_otg_enable(void);

/** Disconnect BATFET (ship mode). Effective only after external VBUS is
 * removed. */
esp_err_t bq25896_power_off(void);

/** Compatibility helper: returns zero on an I2C error. Prefer read_register().
 */
uint8_t bq25896_reg_raw(uint8_t reg);

#ifdef __cplusplus
}
#endif

#endif // BQ25896_H

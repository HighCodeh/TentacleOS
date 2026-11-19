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

#ifndef POWER_PROFILE_H
#define POWER_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Replacement nominal ratings reported by the owner; gauge model not qualified. */
#define POWER_PROFILE_BATTERY_MODEL      "Replacement / 2450mAh"
#define POWER_PROFILE_CAPACITY_SMALL_MAH 2000U
#define POWER_PROFILE_CAPACITY_MAH       2450U
#define POWER_PROFILE_ENERGY_MWH         9065U
#define POWER_PROFILE_GAUGE_QUALIFIED    false
/* Retained lower charge limits; these do not certify the replacement pack. */
#define POWER_PROFILE_CHARGE_MAX_MV      4200U
#define POWER_PROFILE_CHARGE_STANDARD_MA 400U
#define POWER_PROFILE_CHARGE_MAX_MA      1000U
#define POWER_PROFILE_TERMINATION_MA     40U
#define POWER_PROFILE_PRECHARGE_MA       20U
#define POWER_PROFILE_DISCHARGE_MAX_MA   1000U
#define POWER_PROFILE_CUTOFF_MV          3000U
#define POWER_PROFILE_CHARGE_MIN_DECIC   100
#define POWER_PROFILE_CHARGE_MAX_DECIC   450
#define POWER_PROFILE_NTC_OHMS           10000U
#define POWER_PROFILE_NTC_BETA_K         3380U

#define POWER_PROFILE_MOTOR_MODEL    "DM-YX406A-3 / ERM"
#define POWER_PROFILE_MOTOR_RATED_MV 3300U
#define POWER_PROFILE_MOTOR_MIN_MV   2500U
#define POWER_PROFILE_MOTOR_MAX_MV   3600U
#define POWER_PROFILE_MOTOR_MAX_MA   140U
#define POWER_PROFILE_MOTOR_STALL_MA 150U

#define POWER_PROFILE_BQ_CURRENT_STEP_MA    64U
#define POWER_PROFILE_BQ_VOLTAGE_BASE_MV    3840U
#define POWER_PROFILE_BQ_VOLTAGE_STEP_MV    16U
#define POWER_PROFILE_BQ_TERMINATION_MIN_MA 64U

static inline uint16_t power_profile_charge_current_ma(uint16_t requested_ma) {
  uint16_t limited = requested_ma > POWER_PROFILE_CHARGE_STANDARD_MA
                         ? POWER_PROFILE_CHARGE_STANDARD_MA
                         : requested_ma;
  return (limited / POWER_PROFILE_BQ_CURRENT_STEP_MA) * POWER_PROFILE_BQ_CURRENT_STEP_MA;
}

/* Zero means unrepresentable: never round a caller's voltage limit upward. */
static inline uint16_t power_profile_charge_voltage_mv(uint16_t requested_mv) {
  if (requested_mv < POWER_PROFILE_BQ_VOLTAGE_BASE_MV)
    return 0;
  uint16_t limited =
      requested_mv > POWER_PROFILE_CHARGE_MAX_MV ? POWER_PROFILE_CHARGE_MAX_MV : requested_mv;
  return POWER_PROFILE_BQ_VOLTAGE_BASE_MV +
         ((limited - POWER_PROFILE_BQ_VOLTAGE_BASE_MV) / POWER_PROFILE_BQ_VOLTAGE_STEP_MV) *
             POWER_PROFILE_BQ_VOLTAGE_STEP_MV;
}

static inline bool power_profile_temperature_in_range(int16_t temperature_decic) {
  return temperature_decic >= POWER_PROFILE_CHARGE_MIN_DECIC &&
         temperature_decic <= POWER_PROFILE_CHARGE_MAX_DECIC;
}

/* Driver integration must still validate identity, sensing, timing and faults. */
static inline bool power_profile_motor_voltage_valid(uint16_t voltage_mv) {
  return voltage_mv == 0 ||
         (voltage_mv >= POWER_PROFILE_MOTOR_MIN_MV && voltage_mv <= POWER_PROFILE_MOTOR_RATED_MV);
}

#ifdef __cplusplus
}
#endif
#endif

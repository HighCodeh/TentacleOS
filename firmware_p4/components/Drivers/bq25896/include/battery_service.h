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

#ifndef BATTERY_SERVICE_H
#define BATTERY_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "bq25896.h"

/**
 * @brief Cached, smoothed battery/charger state — a single source of truth the
 *        UI reads instead of each screen polling the BQ25896 itself.
 */
typedef struct {
  int soc;                     ///< Qualified state of charge (0-100), or -1 if unknown.
  uint16_t vbat_mv;            ///< Battery voltage in mV.
  bool present;                ///< Valid battery voltage, independent of gauge qualification.
  bool charging;               ///< True while pre/fast charging.
  bool vbus_present;           ///< True when a valid USB/adapter source is present.
  bool low;                    ///< Latched low-battery flag (hysteresis, cleared on charge).
  bool valid;                  ///< True once at least one real read has completed.
  bq25896_charge_status_t chg; ///< Raw charge status.
} battery_snapshot_t;

/**
 * @brief Start the battery poll task (idempotent). Requires bq25896_init() first.
 *
 * Takes one synchronous reading so the first UI paint already has real data, then
 * polls in the background and keeps a smoothed snapshot.
 */
void battery_service_init(void);

/** Charger telemetry is independent of an available/qualified gauge estimate. */
typedef struct {
  bool valid;
  bool requested;
  bool gauge_valid;
  bool current_valid;
  int soc;
  int16_t battery_current_ma;
  bq25896_telem_t charger;
  const char *status; /* Static product-facing reason, no caller ownership. */
} battery_charge_snapshot_t;
bool battery_service_get_charge(battery_charge_snapshot_t *out);
/** Queue a request; all I2C and safety checks run outside the UI task. */
esp_err_t battery_service_request_charge(bool enable);

/**
 * @brief Copy the latest snapshot into @p out.
 *
 * @param out  Destination. Must not be NULL.
 * @return true if a valid reading is available, false otherwise.
 */
bool battery_service_get(battery_snapshot_t *out);

/** @brief Convenience: smoothed SoC (0-100), or -1 if no valid reading yet. */
int battery_service_soc(void);

/** @brief Convenience: latched low-battery flag. */
bool battery_service_is_low(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_SERVICE_H

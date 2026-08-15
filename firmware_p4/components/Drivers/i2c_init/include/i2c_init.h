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

#ifndef I2C_INIT_H
#define I2C_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

#include "driver/i2c_master.h"

// 100 kHz standard-mode, shared by every device on the bus. The V2 board pulls
// SDA/SCL up with 10k (R12/R13/R14 to 3.3VF) plus 22R series (R9/R10) — too weak
// for 400 kHz fast-mode (rise time can't reach VIH in a bit period), which is why
// the BQ25896 NACKs at 400 kHz.
#define I2C_MASTER_FREQ_HZ 100000

/**
 * @brief Initialize the I2C master bus on I2C_NUM_0.
 *
 * Runs a bus-recovery sequence first (clocks out a slave that is holding SDA low
 * after a partial reset or power glitch) so a stuck bus does not leave the
 * charger/haptic/LED unreachable for the whole session.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t init_i2c(void);

/** @brief The shared master bus handle; each device driver adds itself to it. */
i2c_master_bus_handle_t i2c_get_bus(void);

/**
 * @brief Reset a wedged bus at runtime (reactive recovery).
 *
 * Consumers call this after repeated transfer failures. Increments the recovery
 * counter exposed by i2c_recover_count().
 */
esp_err_t i2c_bus_recover(void);

/** @brief Number of runtime bus recoveries performed (for sys_monitor health). */
uint32_t i2c_recover_count(void);

#ifdef __cplusplus
}
#endif

#endif // I2C_INIT_H

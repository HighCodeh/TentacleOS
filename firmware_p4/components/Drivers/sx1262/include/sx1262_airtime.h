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

#ifndef SX1262_AIRTIME_H
#define SX1262_AIRTIME_H

#include <stdint.h>

#include "esp_err.h"
#include "sx1262_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Rounded-up LoRa airtime, using the driver's actual LDRO configuration. */
uint64_t sx1262_airtime_estimate_us(const sx1262_config_t *config, uint8_t length);

/** Reserve airtime before touching TX hardware. Failed TX attempts stay charged.
 *  Reservations survive driver reinitialization and protocol changes until reboot.
 *  ESP_ERR_TIMEOUT means retry later; this function never sleeps. */
esp_err_t sx1262_airtime_reserve(const sx1262_config_t *config, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif // SX1262_AIRTIME_H

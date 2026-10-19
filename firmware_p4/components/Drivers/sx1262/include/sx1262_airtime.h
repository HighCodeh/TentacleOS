// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
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

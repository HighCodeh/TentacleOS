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

#ifndef SUBGHZ_SETTINGS_H
#define SUBGHZ_SETTINGS_H

#include <stdint.h>

#include "cc1101.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared Sub-GHz radio settings chosen in the Radio Config screen and
 *        consumed by the Read/scan screen.
 */
typedef struct {
  cc1101_preset_t preset; /**< @brief CC1101 preset used when starting RX. */
  uint32_t freq;          /**< @brief RX frequency in Hz; 0 means frequency hopping. */
} subghz_settings_t;

/**
 * @brief Get the current shared Sub-GHz settings.
 */
subghz_settings_t subghz_settings_get(void);

/**
 * @brief Set the preset used when starting RX.
 */
void subghz_settings_set_preset(cc1101_preset_t preset);

/**
 * @brief Set the RX frequency in Hz (0 = frequency hopping).
 */
void subghz_settings_set_freq(uint32_t freq);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_SETTINGS_H

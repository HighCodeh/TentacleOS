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

#ifndef TEST_SUBGHZ_CC1101_H
#define TEST_SUBGHZ_CC1101_H
#include <stdint.h>
#include "esp_err.h"
typedef int cc1101_preset_t;
#define CC1101_PRESET_IDLE 0
#define CC1101_PRESET_OOK_270KHZ 1
#define CC1101_PRESET_OOK_650KHZ 2
#define CC1101_PRESET_2FSK_2KHZ 3
#define CC1101_PRESET_2FSK_47KHZ 4
#define CC1101_PRESET_OOK_800KHZ 6
#define CC1101_PRESET_COUNT 7
uint8_t cc1101_get_active_preset_id(void);
esp_err_t cc1101_set_frequency(uint32_t frequency);
#endif

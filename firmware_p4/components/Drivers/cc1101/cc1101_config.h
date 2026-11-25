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

#ifndef CC1101_CONFIG_H
#define CC1101_CONFIG_H

#include "cc1101.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC1101_PROFILE_REGISTER_COUNT 15
#define CC1101_PROFILE_MDMCFG4        0
#define CC1101_PROFILE_MDMCFG3        1
#define CC1101_PROFILE_DEVIATN        5
#define CC1101_PROFILE_AGCCTRL2       9

typedef struct {
  uint8_t modulation;
  uint8_t values[CC1101_PROFILE_REGISTER_COUNT];
} cc1101_async_profile_t;

extern const uint8_t CC1101_PROFILE_REGISTERS[CC1101_PROFILE_REGISTER_COUNT];
const cc1101_async_profile_t *cc1101_config_get_profile(cc1101_preset_t preset);

#ifdef __cplusplus
}
#endif

#endif

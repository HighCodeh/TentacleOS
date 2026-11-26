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

#ifndef ETHERNET_POWER_PROFILES_H
#define ETHERNET_POWER_PROFILES_H

#include <stdint.h>

#define ETHERNET_POWER_STARTUP_MA   1400
#define ETHERNET_POWER_DEFAULT_MA   500
#define ETHERNET_POWER_ASIX_VID     0x0B95
#define ETHERNET_POWER_AX88179B_PID 0x1790
#define ETHERNET_POWER_AX88179B_MA  1400

typedef struct {
  uint16_t vid;
  uint16_t pid;
  uint16_t current_ma;
} ethernet_power_profile_t;

// VID/PID become available only after the shared startup budget powers enumeration.
static const ethernet_power_profile_t s_ethernet_power_profiles[] = {
    {ETHERNET_POWER_ASIX_VID, ETHERNET_POWER_AX88179B_PID, ETHERNET_POWER_AX88179B_MA},
};

#endif

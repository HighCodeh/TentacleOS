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

#ifndef HOST_LINK_FLY_H
#define HOST_LINK_FLY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint8_t host_fly_handle(uint16_t cmd,
                        const uint8_t *payload,
                        uint16_t plen,
                        uint8_t *out,
                        uint16_t out_cap,
                        uint16_t *out_len);
#ifdef __cplusplus
}
#endif
#endif

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
#ifndef ISO15693_STREAM_H
#define ISO15693_STREAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ST25R3916 subcarrier stream: high rate, single subcarrier, 1-out-of-4 TX.
size_t iso15693_stream_encode(const uint8_t *data, size_t len, uint8_t *out, size_t capacity);
size_t iso15693_stream_decode(const uint8_t *raw, size_t raw_bits, uint8_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif

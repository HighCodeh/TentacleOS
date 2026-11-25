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

#ifndef FREQUENCY_CATALOG_H
#define FREQUENCY_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREQUENCY_CATALOG_CARRIER_COUNT 52U

static inline const uint32_t *frequency_catalog_get(size_t *out_count) {
  static const uint32_t frequencies[] = {
      300000000U, 302757000U, 303000000U, 303875000U, 304250000U, 307000000U, 307500000U,
      310000000U, 312000000U, 312100000U, 313000000U, 313850000U, 314000000U, 314350000U,
      314980000U, 315000000U, 318000000U, 320000000U, 330000000U, 348000000U, 387000000U,
      390000000U, 418000000U, 430000000U, 430500000U, 431000000U, 431500000U, 433075000U,
      433220000U, 433420000U, 433657070U, 433889000U, 433920000U, 434075000U, 434176948U,
      434420000U, 434620000U, 434775000U, 438900000U, 464000000U, 779000000U, 868000000U,
      868350000U, 868400000U, 868460000U, 868800000U, 868950000U, 869525000U, 906400000U,
      915000000U, 925000000U, 928000000U, 0};
  if (out_count != NULL)
    *out_count = sizeof(frequencies) / sizeof(frequencies[0]);
  return frequencies;
}

static inline uint16_t frequency_catalog_nearest(uint32_t frequency_hz) {
  const uint32_t *frequencies = frequency_catalog_get(NULL);
  uint16_t nearest = 0;
  uint32_t best_distance = UINT32_MAX;
  for (uint16_t i = 0; i < FREQUENCY_CATALOG_CARRIER_COUNT; ++i) {
    uint32_t distance = frequencies[i] > frequency_hz ? frequencies[i] - frequency_hz
                                                      : frequency_hz - frequencies[i];
    if (distance < best_distance) {
      best_distance = distance;
      nearest = i;
    }
  }
  return nearest;
}

#ifdef __cplusplus
}
#endif
#endif

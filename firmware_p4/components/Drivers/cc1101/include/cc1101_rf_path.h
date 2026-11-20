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

#ifndef CC1101_RF_PATH_H
#define CC1101_RF_PATH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC1101_BAND_315_MIN_HZ  300000000UL
#define CC1101_BAND_315_MAX_HZ  348000000UL
#define CC1101_BAND_433_MIN_HZ  387000000UL
#define CC1101_BAND_433_MAX_HZ  464000000UL
#define CC1101_BAND_HIGH_MIN_HZ 779000000UL
#define CC1101_BAND_HIGH_MAX_HZ 928000000UL
/* PA-table boundary; both upper bands use RF2. */
#define CC1101_PATH_915_MIN_HZ 900000000UL

typedef enum {
  CC1101_RF_PATH_NONE = 0,
  CC1101_RF_PATH_1,
  CC1101_RF_PATH_2,
  CC1101_RF_PATH_3,
  CC1101_RF_PATH_4,
  CC1101_RF_PATH_COUNT,
} cc1101_rf_path_t;

/* Prototype link measurements, 13-Nov-2026: RF1 for low bands, RF2 for high bands. */
static inline cc1101_rf_path_t cc1101_rf_path_for_frequency(uint32_t frequency_hz) {
  if (frequency_hz >= CC1101_BAND_315_MIN_HZ && frequency_hz <= CC1101_BAND_315_MAX_HZ)
    return CC1101_RF_PATH_1;
  if (frequency_hz >= CC1101_BAND_433_MIN_HZ && frequency_hz <= CC1101_BAND_433_MAX_HZ)
    return CC1101_RF_PATH_1;
  if (frequency_hz >= CC1101_BAND_HIGH_MIN_HZ && frequency_hz <= CC1101_BAND_HIGH_MAX_HZ)
    return CC1101_RF_PATH_2;
  return CC1101_RF_PATH_NONE;
}

/* PE613050 DOC-32614-9.01 table 4: columns are V2,V1, not V1,V2. */
static inline bool
cc1101_rf_path_levels(cc1101_rf_path_t path, uint32_t *out_v1, uint32_t *out_v2) {
  if (out_v1 == NULL || out_v2 == NULL || path <= CC1101_RF_PATH_NONE ||
      path >= CC1101_RF_PATH_COUNT)
    return false;
  *out_v1 = path == CC1101_RF_PATH_3 || path == CC1101_RF_PATH_4;
  *out_v2 = path == CC1101_RF_PATH_2 || path == CC1101_RF_PATH_4;
  return true;
}

#ifdef __cplusplus
}
#endif
#endif

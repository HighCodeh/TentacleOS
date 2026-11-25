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

#include "frequency_analyzer.h"
#include <stddef.h>
#define COARSE_STEP_HZ 250000U
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const uint32_t COMMON_FREQUENCIES[] = {315000000,
                                              433920000,
                                              868350000,
                                              915000000,
                                              300000000,
                                              303875000,
                                              304250000,
                                              310000000,
                                              318000000,
                                              390000000,
                                              418000000,
                                              433075000,
                                              433420000,
                                              434420000,
                                              434775000,
                                              438900000,
                                              925000000};
static const struct {
  uint32_t low;
  uint32_t high;
} BANDS[] = {
    {300000000, 348000000}, {387000000, 464000000}, {779000000, 899750000}, {900000000, 928000000}};
static const char *const MODE_NAMES[] = {"Auto", "315", "433", "868", "915", "Full"};
const char *frequency_analyzer_mode_name(frequency_analyzer_mode_t mode) {
  return mode < FREQUENCY_ANALYZER_MODE_COUNT && mode >= 0 ? MODE_NAMES[mode] : "?";
}
uint16_t frequency_analyzer_plan_count(frequency_analyzer_mode_t mode) {
  if (mode == FREQUENCY_ANALYZER_COMMON)
    return ARRAY_COUNT(COMMON_FREQUENCIES);
  if (mode <= FREQUENCY_ANALYZER_COMMON || mode >= FREQUENCY_ANALYZER_MODE_COUNT)
    return 0;
  uint16_t count = 0;
  for (size_t b = 0; b < ARRAY_COUNT(BANDS); b++)
    if (mode == FREQUENCY_ANALYZER_ALL || (size_t)mode == b + 1)
      count += (BANDS[b].high - BANDS[b].low) / COARSE_STEP_HZ + 1;
  return count;
}
uint32_t frequency_analyzer_plan_frequency(frequency_analyzer_mode_t mode, uint16_t index) {
  if (index >= frequency_analyzer_plan_count(mode))
    return 0;
  if (mode == FREQUENCY_ANALYZER_COMMON)
    return COMMON_FREQUENCIES[index];
  for (size_t b = 0; b < ARRAY_COUNT(BANDS); b++) {
    if (mode != FREQUENCY_ANALYZER_ALL && (size_t)mode != b + 1)
      continue;
    uint16_t count = (BANDS[b].high - BANDS[b].low) / COARSE_STEP_HZ + 1;
    if (index < count)
      return BANDS[b].low + index * COARSE_STEP_HZ;
    index -= count;
  }
  return 0;
}

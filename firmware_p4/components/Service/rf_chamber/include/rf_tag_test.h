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

#ifndef RF_TAG_TEST_H
#define RF_TAG_TEST_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RF_TAG_TEST_UID_MAX     10U
#define RF_TAG_TEST_DURATION_MS 30000U
typedef enum { RF_TAG_TEST_NFC, RF_TAG_TEST_LF, RF_TAG_TEST_KIND_COUNT } rf_tag_test_kind_t;
typedef enum {
  RF_TAG_TEST_IDLE,
  RF_TAG_TEST_STARTING,
  RF_TAG_TEST_READING,
  RF_TAG_TEST_FOUND,
  RF_TAG_TEST_TIMEOUT,
  RF_TAG_TEST_CANCELLED,
  RF_TAG_TEST_ERROR,
  RF_TAG_TEST_HELD,
  RF_TAG_TEST_PHASE_COUNT
} rf_tag_test_phase_t;
typedef struct {
  rf_tag_test_kind_t kind;
  rf_tag_test_phase_t phase;
  esp_err_t error;
  esp_err_t cleanup_error;
  bool busy;
  uint32_t attempts;
  uint32_t elapsed_ms;
  uint32_t cl_hz;
  uint32_t da_edges;
  uint32_t dropped;
  uint32_t invalid_pulses;
  uint8_t uid[RF_TAG_TEST_UID_MAX];
  uint8_t uid_len;
  uint8_t tech;
  uint8_t protocol;
  uint8_t atqa[2];
  uint8_t sak;
} rf_tag_test_status_t;
esp_err_t rf_tag_test_start(rf_tag_test_kind_t kind);
void rf_tag_test_stop(void);
void rf_tag_test_snapshot(rf_tag_test_status_t *out);
bool rf_tag_test_busy(void);
#ifdef __cplusplus
}
#endif
#endif

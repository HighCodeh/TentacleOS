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

#include "tracker_detector.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"

static const char *TAG = "TRACKER_DETECTOR";

#define SPI_DATA_TIMEOUT_MS 1000

static tracker_detector_record_t *s_cached_results = NULL;
static uint16_t s_cached_count = 0;
static uint16_t s_cached_capacity = 0;
static tracker_detector_record_t s_empty_record;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

bool tracker_detector_start(void) {
  ESP_LOGI(TAG, "Tracker detector started");
  tracker_detector_clear_results();
  s_session_id = spi_session_start(SPI_ID_BT_APP_TRACKER, NULL, 0, NULL, NULL);
  return s_session_id != SPI_SESSION_INVALID_ID;
}

void tracker_detector_stop(void) {
  ESP_LOGI(TAG, "Tracker detector stopped");
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
  tracker_detector_clear_results();
}

tracker_detector_record_t *tracker_detector_get_results(uint16_t *out_count) {
  spi_header_t resp;
  uint8_t payload[2];
  uint16_t magic_count = SPI_DATA_INDEX_COUNT;

  if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                              (uint8_t *)&magic_count,
                              sizeof(magic_count),
                              &resp,
                              payload,
                              sizeof(payload),
                              SPI_DATA_TIMEOUT_MS) != ESP_OK) {
    if (out_count != NULL) {
      *out_count = s_cached_count;
    }
    return s_cached_results != NULL ? s_cached_results : &s_empty_record;
  }

  uint16_t remote_count = 0;
  memcpy(&remote_count, payload, sizeof(remote_count));
  if (remote_count > TRACKER_DETECTOR_MAX_RESULTS) {
    remote_count = TRACKER_DETECTOR_MAX_RESULTS;
  }

  if (remote_count < s_cached_count) {
    s_cached_count = 0;
  }

  if (remote_count > s_cached_capacity) {
    tracker_detector_record_t *new_buf = (tracker_detector_record_t *)realloc(
        s_cached_results, remote_count * sizeof(tracker_detector_record_t));
    if (new_buf == NULL) {
      if (out_count != NULL) {
        *out_count = s_cached_count;
      }
      return s_cached_results != NULL ? s_cached_results : &s_empty_record;
    }
    s_cached_results = new_buf;
    s_cached_capacity = remote_count;
  }

  uint16_t fetched = s_cached_count;
  for (uint16_t i = s_cached_count; i < remote_count; i++) {
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                                (uint8_t *)&i,
                                sizeof(i),
                                &resp,
                                (uint8_t *)&s_cached_results[i],
                                sizeof(s_cached_results[i]),
                                SPI_DATA_TIMEOUT_MS) != ESP_OK) {
      break;
    }
    fetched = i + 1;
  }

  s_cached_count = fetched;
  if (out_count != NULL) {
    *out_count = s_cached_count;
  }
  return s_cached_results != NULL ? s_cached_results : &s_empty_record;
}

void tracker_detector_clear_results(void) {
  if (s_cached_results != NULL) {
    free(s_cached_results);
    s_cached_results = NULL;
  }
  s_cached_count = 0;
  s_cached_capacity = 0;
}

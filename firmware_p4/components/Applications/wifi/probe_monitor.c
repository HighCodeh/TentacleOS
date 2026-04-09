// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "probe_monitor.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

#include "spi_bridge.h"

static const char *TAG = "PROBE_MONITOR";

#define PROBE_MONITOR_MAX_RESULTS 300

static probe_monitor_record_t s_cached_probe;
static probe_monitor_record_t *s_cached_results = NULL;
static uint16_t s_cached_count = 0;
static uint16_t s_cached_capacity = 0;
static probe_monitor_record_t s_empty_record;

bool probe_monitor_start(void) {
  probe_monitor_free_results();
  return (spi_bridge_send_command(SPI_ID_WIFI_APP_PROBE_MON, NULL, 0, NULL, NULL, 2000) == ESP_OK);
}

void probe_monitor_stop(void) {
  spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
  probe_monitor_free_results();
}

probe_monitor_record_t *probe_monitor_get_results(uint16_t *out_count) {
  spi_header_t resp;
  uint8_t payload[2];
  uint16_t magic_count = SPI_DATA_INDEX_COUNT;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&magic_count, 2, &resp, payload, 1000) != ESP_OK) {
    if (out_count != NULL)
      *out_count = s_cached_count;
    return s_cached_results != NULL ? s_cached_results : &s_empty_record;
  }

  uint16_t remote_count = 0;
  memcpy(&remote_count, payload, 2);
  if (remote_count > PROBE_MONITOR_MAX_RESULTS) {
    remote_count = PROBE_MONITOR_MAX_RESULTS;
  }

  if (remote_count < s_cached_count) {
    s_cached_count = 0;
  }

  if (remote_count > s_cached_capacity) {
    probe_monitor_record_t *new_buf = (probe_monitor_record_t *)realloc(
        s_cached_results, remote_count * sizeof(probe_monitor_record_t));
    if (new_buf == NULL) {
      if (out_count != NULL)
        *out_count = s_cached_count;
      return s_cached_results != NULL ? s_cached_results : &s_empty_record;
    }
    s_cached_results = new_buf;
    s_cached_capacity = remote_count;
  }

  uint16_t fetched = s_cached_count;
  for (uint16_t i = s_cached_count; i < remote_count; i++) {
    if (spi_bridge_send_command(
            SPI_ID_SYSTEM_DATA, (uint8_t *)&i, 2, &resp, (uint8_t *)&s_cached_results[i], 1000) !=
        ESP_OK) {
      break;
    }
    fetched = i + 1;
  }

  s_cached_count = fetched;
  if (out_count != NULL)
    *out_count = s_cached_count;
  return s_cached_results != NULL ? s_cached_results : &s_empty_record;
}

probe_monitor_record_t *probe_monitor_get_result_by_index(uint16_t index) {
  spi_header_t resp;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&index, 2, &resp, (uint8_t *)&s_cached_probe, 1000) ==
      ESP_OK) {
    return &s_cached_probe;
  }
  return NULL;
}

void probe_monitor_free_results(void) {
  if (s_cached_results != NULL) {
    free(s_cached_results);
    s_cached_results = NULL;
  }
  s_cached_count = 0;
  s_cached_capacity = 0;
}

bool probe_monitor_save_results_to_internal_flash(void) {
  return (spi_bridge_send_command(SPI_ID_WIFI_PROBE_SAVE_FLASH, NULL, 0, NULL, NULL, 5000) ==
          ESP_OK);
}

bool probe_monitor_save_results_to_sd_card(void) {
  return (spi_bridge_send_command(SPI_ID_WIFI_PROBE_SAVE_SD, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}

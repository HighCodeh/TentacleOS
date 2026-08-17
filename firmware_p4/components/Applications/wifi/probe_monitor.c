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

#include "probe_monitor.h"

#include "led_control.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"

static const char *TAG = "PROBE_MONITOR";

#define PROBE_MONITOR_MAX_RESULTS 300

static probe_monitor_record_t s_cached_probe;
static probe_monitor_record_t *s_cached_results = NULL;
static uint16_t s_cached_count = 0;
static uint16_t s_cached_capacity = 0;
static probe_monitor_record_t s_empty_record;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

bool probe_monitor_start(void) {
  probe_monitor_free_results();
  s_session_id = spi_session_start(SPI_ID_WIFI_APP_PROBE_MON, NULL, 0, NULL, NULL);
  bool ok = s_session_id != SPI_SESSION_INVALID_ID;
  ok ? led_signal_info() : led_signal_error();
  return ok;
}

void probe_monitor_stop(void) {
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
  probe_monitor_free_results();
}

probe_monitor_record_t *probe_monitor_get_results(uint16_t *out_count) {
  spi_header_t resp;
  uint8_t payload[2];
  uint16_t magic_count = SPI_DATA_INDEX_COUNT;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&magic_count, 2, &resp, payload, sizeof(payload), 1000) !=
      ESP_OK) {
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
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                                (uint8_t *)&i,
                                2,
                                &resp,
                                (uint8_t *)&s_cached_results[i],
                                sizeof(s_cached_results[i]),
                                1000) != ESP_OK) {
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
  if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                              (uint8_t *)&index,
                              2,
                              &resp,
                              (uint8_t *)&s_cached_probe,
                              sizeof(s_cached_probe),
                              1000) == ESP_OK) {
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
  return (spi_bridge_send_command(SPI_ID_WIFI_PROBE_SAVE_FLASH, NULL, 0, NULL, NULL, 0, 5000) ==
          ESP_OK);
}

bool probe_monitor_save_results_to_sd_card(void) {
  return (spi_bridge_send_command(SPI_ID_WIFI_PROBE_SAVE_SD, NULL, 0, NULL, NULL, 0, 5000) ==
          ESP_OK);
}

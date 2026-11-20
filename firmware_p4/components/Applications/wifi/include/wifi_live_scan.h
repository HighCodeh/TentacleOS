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

#ifndef WIFI_LIVE_SCAN_H
#define WIFI_LIVE_SCAN_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "wifi_service.h"
#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_LIVE_SCAN_MAX_APS 128

typedef struct {
  int32_t rssi_sum;
  uint16_t samples;
  int8_t rssi_min;
  int8_t rssi_max;
} wifi_live_scan_observation_t;

typedef struct {
  bool is_running;
  bool is_full;
  uint16_t count;
  uint32_t revision;
  uint32_t sweeps;
  esp_err_t error;
} wifi_live_scan_status_t;

esp_err_t wifi_live_scan_start(uint8_t band);
void wifi_live_scan_stop(void);
esp_err_t wifi_live_scan_get_observation(uint16_t index, wifi_live_scan_observation_t *out_stats);
void wifi_live_scan_get_status(wifi_live_scan_status_t *out_status);
esp_err_t wifi_live_scan_get_record(uint16_t index, wifi_ap_record_t *out_record);

#ifdef __cplusplus
}
#endif
#endif

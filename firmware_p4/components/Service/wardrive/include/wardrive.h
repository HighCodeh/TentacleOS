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

#ifndef WARDRIVE_H
#define WARDRIVE_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "wardrive_gps.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WARDRIVE_UI_ROWS    4
#define WARDRIVE_PATH_BYTES 96

typedef struct {
  char ssid[33];
  int8_t rssi;
  uint8_t channel, auth;
  bool located;
} wardrive_row_t;
typedef struct {
  bool running, stopping, paused, scanning, gps_online, has_fix, storage_ok, unique_full;
  uint8_t band, satellites, visible;
  uint32_t elapsed_s, cycles, observations, unique, saved, located, buffered, dropped;
  uint32_t uart_errors, checksum_errors, gps_bytes, scan_errors, storage_errors, retry_s;
  uint32_t session_id;
  esp_err_t radio_error, storage_error, gps_error;
  wardrive_fix_t fix;
  char path[WARDRIVE_PATH_BYTES];
  wardrive_row_t rows[WARDRIVE_UI_ROWS];
} wardrive_status_t;
esp_err_t wardrive_start(uint8_t band);
void wardrive_stop(void);
void wardrive_pause(bool paused);
void wardrive_get_status(wardrive_status_t *out_status);
bool wardrive_is_running(void);
#ifdef __cplusplus
}
#endif
#endif

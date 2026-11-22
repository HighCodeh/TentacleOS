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

#ifndef WIFI_UDP_SCAN_H
#define WIFI_UDP_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_UDP_SCAN_MAX_PORTS    32
#define WIFI_UDP_SCAN_BANNER_BYTES 64

typedef enum {
  WIFI_UDP_SCAN_OPEN = 0,
  WIFI_UDP_SCAN_OPEN_FILTERED,
  WIFI_UDP_SCAN_FILTERED,
  WIFI_UDP_SCAN_STATUS_COUNT
} wifi_udp_scan_status_t;

typedef struct {
  uint16_t port;
  wifi_udp_scan_status_t status;
  char banner[WIFI_UDP_SCAN_BANNER_BYTES];
} wifi_udp_scan_result_t;

typedef void (*wifi_udp_scan_result_cb_t)(const wifi_udp_scan_result_t *result, void *context);

typedef struct {
  const char *ip;
  const int *ports;
  uint16_t count;
  wifi_udp_scan_result_cb_t on_result;
  void *context;
} wifi_udp_scan_request_t;

// Caller retains the request until this synchronous, cancellable scan returns.
esp_err_t wifi_udp_scan_run(const wifi_udp_scan_request_t *request);
void wifi_udp_scan_cancel(void);
// Only reset while the shared scan runner is idle.
void wifi_udp_scan_reset(void);

#ifdef __cplusplus
}
#endif
#endif

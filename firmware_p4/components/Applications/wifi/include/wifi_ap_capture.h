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

#ifndef WIFI_AP_CAPTURE_H
#define WIFI_AP_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_AP_CAPTURE_RECENT_COUNT  8
#define WIFI_AP_CAPTURE_FILENAME_SIZE 64
#define WIFI_AP_CAPTURE_MAC_SIZE      6

typedef struct {
  uint8_t bssid[WIFI_AP_CAPTURE_MAC_SIZE];
  char filename[WIFI_AP_CAPTURE_FILENAME_SIZE];
  uint32_t frames;
  bool has_ssid;
} wifi_ap_capture_saved_t;

typedef struct {
  wifi_ap_capture_saved_t recent[WIFI_AP_CAPTURE_RECENT_COUNT];
  size_t count;
  size_t bytes;
  uint32_t frames;
  uint32_t pending;
  uint32_t queue_peak;
  uint32_t dropped;
  uint32_t discarded;
} wifi_ap_capture_snapshot_t;

void wifi_ap_capture_snapshot(wifi_ap_capture_snapshot_t *out_snapshot);

/* Caller serializes all operations with the sniffer capture mutex. */
esp_err_t wifi_ap_capture_start(void);
esp_err_t wifi_ap_capture_frame(const uint8_t *frame, size_t len);
esp_err_t
wifi_ap_capture_frame_at(const uint8_t *frame, size_t len, const struct timeval *received_at);
void wifi_ap_capture_stop(void);
size_t wifi_ap_capture_bytes(void);

#ifdef __cplusplus
}
#endif
#endif

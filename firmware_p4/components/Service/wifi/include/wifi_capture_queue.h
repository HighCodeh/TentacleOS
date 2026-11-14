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

#ifndef WIFI_CAPTURE_QUEUE_H
#define WIFI_CAPTURE_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CAPTURE_QUEUE_FRAME_MAX 2048
#define WIFI_CAPTURE_QUEUE_CAPACITY  64

typedef struct {
  struct timeval received_at;
  uint16_t len;
  uint8_t data[WIFI_CAPTURE_QUEUE_FRAME_MAX];
} wifi_capture_queue_frame_t;

typedef struct {
  uint32_t pending;
  uint32_t peak;
  uint32_t dropped;
  uint32_t discarded;
  esp_err_t error;
} wifi_capture_queue_stats_t;

/* The worker owns frame storage; the callback must not retain its pointer. */
typedef esp_err_t (*wifi_capture_queue_write_cb_t)(const wifi_capture_queue_frame_t *frame);

esp_err_t wifi_capture_queue_start(wifi_capture_queue_write_cb_t write_cb);
/* Copies data; never waits for SD or queue space. */
esp_err_t wifi_capture_queue_push(const uint8_t *data, size_t len);
/* Closes admission, drains accepted frames, joins the worker, then frees PSRAM. */
esp_err_t wifi_capture_queue_stop(void);
void wifi_capture_queue_get_stats(wifi_capture_queue_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
#endif

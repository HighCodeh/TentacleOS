// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WIFI_SCAN_VIEW_H
#define WIFI_SCAN_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  spi_wifi_quick_status_t capture;
  esp_err_t error;
  uint32_t revision;
  uint16_t count;
  bool is_busy;
} wifi_scan_view_status_t;

/** Run one existing AP scan on the radio worker. seconds is a legacy preset, not a duration. */
esp_err_t wifi_scan_view_start(uint8_t seconds);
/** Cooperative cancellation, safe from the UI thread and on screen teardown. */
void wifi_scan_view_cancel(void);
void wifi_scan_view_get_status(wifi_scan_view_status_t *out_status);
esp_err_t wifi_scan_view_get_record(uint16_t index, spi_wifi_quick_record_t *out_record);

#ifdef __cplusplus
}
#endif
#endif

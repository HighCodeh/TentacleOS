// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WIFI_QUICK_SCAN_CORE_H
#define WIFI_QUICK_SCAN_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_QUICK_SCAN_MAX_RECORDS   128
#define WIFI_QUICK_SCAN_WHITELIST_MAX 32
#define WIFI_QUICK_SCAN_WINDOW_MS     5000

typedef struct {
  uint8_t mac[SPI_WIFI_QUICK_MAC_LEN];
  uint8_t channel;
  int8_t rssi;
  uint16_t evidence;
  uint32_t elapsed_ms;
  uint8_t bssid[SPI_WIFI_QUICK_MAC_LEN];
  char ssid[SPI_WIFI_QUICK_SSID_LEN];
  uint8_t ssid_state;
} wifi_quick_scan_core_observation_t;

typedef struct {
  spi_wifi_quick_record_t record;
  int64_t rssi_sum;
  uint16_t windows;
} wifi_quick_scan_core_entry_t;

typedef struct {
  wifi_quick_scan_core_entry_t entries[WIFI_QUICK_SCAN_MAX_RECORDS];
  uint16_t count;
  uint32_t overflow;
} wifi_quick_scan_core_t;

/** Frame excludes the trailing FCS; only frames with a transmitter address are accepted. */
esp_err_t wifi_quick_scan_core_parse(const uint8_t *frame,
                                     size_t length,
                                     wifi_quick_scan_core_observation_t *out_observation);
void wifi_quick_scan_core_observe(wifi_quick_scan_core_t *core,
                                  const wifi_quick_scan_core_observation_t *observation);
/** Identity evidence is correlated: OUI and ESP-NOW contribute max(45,25), not their sum. */
void wifi_quick_scan_core_finish(wifi_quick_scan_core_t *core,
                                 uint32_t duration_ms,
                                 const uint8_t (*whitelist)[SPI_WIFI_QUICK_MAC_LEN],
                                 size_t whitelist_count);

#ifdef __cplusplus
}
#endif

#endif

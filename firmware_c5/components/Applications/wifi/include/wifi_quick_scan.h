// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WIFI_QUICK_SCAN_H
#define WIFI_QUICK_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** C5: initialize once before concurrent calls. P4: no local resources. */
esp_err_t wifi_quick_scan_init(void);
/** Explicit, asynchronous start; NULL selects 45 seconds and both bands.
 * Normal Wi-Fi must be stopped first; an active radio is rejected, never disconnected.
 * All buffers are owned by the module; each successful start replaces the previous result.
 */
esp_err_t wifi_quick_scan_start(const spi_wifi_quick_config_t *config);
/** Request cancellation; poll status until CANCELLED/ERROR before fetching partial results. */
esp_err_t wifi_quick_scan_stop(void);
esp_err_t wifi_quick_scan_get_status(spi_wifi_quick_status_t *out_status);
/** Stable sorted records after completion/cancellation; caller owns the copied record.
 * Filtered indices omit authorized entries unless include_authorized is true.
 */
esp_err_t wifi_quick_scan_get_record(uint16_t index,
                                     bool include_authorized,
                                     spi_wifi_quick_record_t *out_record);
/** Exact-MAC whitelist, max 32, RAM-only. Updates require an idle/completed scan.
 * Updated authorization applies on the next scan. Unknown does not mean unauthorized.
 */
esp_err_t wifi_quick_scan_set_authorized(const uint8_t mac[SPI_WIFI_QUICK_MAC_LEN],
                                         bool is_authorized);

#ifdef __cplusplus
}
#endif

#endif

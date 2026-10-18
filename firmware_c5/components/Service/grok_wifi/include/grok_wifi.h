// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GROK_WIFI_H
#define GROK_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "spi_protocol.h"

// No work is performed at boot. START is explicit and never enables Wi-Fi.
// The caller must poll STATUS (or transfer data) at least every 10 seconds.
#ifdef __cplusplus
extern "C" {
#endif

bool grok_wifi_is_active(void);
void grok_wifi_stop(void);
spi_status_t grok_wifi_execute(
    spi_id_t id, const uint8_t *payload, uint8_t len, uint8_t *response, uint8_t *response_len);

#ifdef __cplusplus
}
#endif

#endif

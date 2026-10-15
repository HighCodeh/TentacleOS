// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_link_fly.h"

#include "esp_log.h"

#include "fly_link.h"
#include "spi_protocol.h"

static const char *TAG = "HOST_FLY";

uint8_t host_fly_handle(uint16_t cmd,
                        const uint8_t *payload,
                        uint16_t plen,
                        uint8_t *out,
                        uint16_t out_cap,
                        uint16_t *out_len) {
  uint8_t result = fly_link_handle(cmd, payload, plen, out, out_cap, out_len);
  if (result != SPI_STATUS_OK)
    ESP_LOGD(TAG, "Command 0x%04x status %u", cmd, result);
  return result;
}

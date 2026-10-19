// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HOST_LINK_RF_H
#define HOST_LINK_RF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_RF_RESPONSE_MAX 17

typedef struct {
  uint16_t command;
  const uint8_t *payload;
  uint16_t length;
  uint8_t *out;
  uint16_t capacity;
  uint16_t out_length;
} host_rf_request_t;

// Invoked only by the authenticated host-link worker; response is caller-owned.
uint8_t host_rf_handle(host_rf_request_t *request);
void host_rf_stop(void);

#ifdef __cplusplus
}
#endif
#endif

// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include <stdint.h>

#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t command;
  const uint8_t *payload;
  uint8_t length;
  uint8_t *out;
  uint8_t *out_length;
} mqtt_bridge_request_t;

/** SPI worker only. MQTT callbacks share only the bounded queue and counters. */
spi_status_t mqtt_bridge_execute(const mqtt_bridge_request_t *request);

#ifdef __cplusplus
}
#endif
#endif // MQTT_BRIDGE_H

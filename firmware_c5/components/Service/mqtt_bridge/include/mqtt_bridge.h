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

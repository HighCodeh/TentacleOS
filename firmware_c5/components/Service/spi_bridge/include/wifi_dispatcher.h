// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef WIFI_DISPATCHER_H
#define WIFI_DISPATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "spi_protocol.h"

/**
 * @brief Execute a WiFi command received via SPI.
 *
 * @param id               Command identifier.
 * @param payload          Pointer to the command payload.
 * @param len              Payload length in bytes.
 * @param out_resp_payload Buffer to fill with the response data.
 * @param out_resp_len     Pointer to store the response length.
 * @return SPI status code indicating the operation result.
 */
spi_status_t wifi_dispatcher_execute(spi_id_t id,
                                     const uint8_t *payload,
                                     uint8_t len,
                                     uint8_t *out_resp_payload,
                                     uint8_t *out_resp_len);

#ifdef __cplusplus
}
#endif

#endif // WIFI_DISPATCHER_H

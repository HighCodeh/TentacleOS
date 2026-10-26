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

#ifndef HOST_LINK_RF_H
#define HOST_LINK_RF_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

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

#define HOST_RF_NFC_KEY_SIZE   6
#define HOST_RF_NFC_BLOCK_SIZE 16

typedef struct {
  uint8_t block;
  bool is_write;
  uint8_t key[HOST_RF_NFC_KEY_SIZE];
  uint8_t data[HOST_RF_NFC_BLOCK_SIZE];
} host_rf_nfc_block_t;

/** Persistent host worker only; bounded Classic 1K Key A access, owned field/cleanup. */
esp_err_t host_rf_nfc_block(host_rf_nfc_block_t *request);

// Invoked only by the authenticated host-link worker; response is caller-owned.
uint8_t host_rf_handle(host_rf_request_t *request);
void host_rf_stop(void);

#ifdef __cplusplus
}
#endif
#endif

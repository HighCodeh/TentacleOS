// SPDX-License-Identifier: GPL-3.0-or-later
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


#ifndef TEST_FIRMWARE_CRYPTO_H
#define TEST_FIRMWARE_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef unsigned psa_algorithm_t;
#define PSA_ALG_SHA_256 1U
#define PSA_ALG_HMAC(x) (2U + (x))
#define PSA_ALG_CTR 4U
typedef struct { const uint8_t *data; size_t size; } firmware_crypto_span_t;
typedef struct {
  firmware_crypto_span_t key, iv;
  psa_algorithm_t algorithm;
  bool is_encrypt;
} firmware_crypto_cipher_t;
typedef struct {
  firmware_crypto_span_t secret, salt, info;
  bool is_expand_only;
} firmware_crypto_hkdf_t;
esp_err_t firmware_crypto_mac(firmware_crypto_span_t, firmware_crypto_span_t, psa_algorithm_t, uint8_t *);
esp_err_t firmware_crypto_hkdf(const firmware_crypto_hkdf_t *, uint8_t *, size_t);
esp_err_t firmware_crypto_cipher(const firmware_crypto_cipher_t *, firmware_crypto_span_t, uint8_t *);
#endif

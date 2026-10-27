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

#ifndef FIRMWARE_CRYPTO_H
#define FIRMWARE_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "psa/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_CRYPTO_SHA256_SIZE 32U
#define FIRMWARE_CRYPTO_AES_BLOCK_SIZE 16U
#define FIRMWARE_CRYPTO_X25519_BITS 255U
#define FIRMWARE_CRYPTO_X25519_SIZE 32U

typedef struct {
  const uint8_t *data;
  size_t size;
} firmware_crypto_span_t;

typedef struct {
  firmware_crypto_span_t key;
  firmware_crypto_span_t iv;
  psa_algorithm_t algorithm;
  bool is_encrypt;
} firmware_crypto_cipher_t;

typedef struct {
  firmware_crypto_cipher_t cipher;
  firmware_crypto_span_t aad;
  uint8_t *tag;
  size_t tag_size;
} firmware_crypto_aead_t;

typedef struct {
  firmware_crypto_span_t secret;
  firmware_crypto_span_t salt;
  firmware_crypto_span_t info;
  bool is_expand_only;
} firmware_crypto_hkdf_t;

esp_err_t firmware_crypto_sha256(firmware_crypto_span_t input, uint8_t out_data[32]);
esp_err_t firmware_crypto_mac(firmware_crypto_span_t key, firmware_crypto_span_t input,
                             psa_algorithm_t algorithm, uint8_t *out_data);
esp_err_t firmware_crypto_hkdf(const firmware_crypto_hkdf_t *params, uint8_t *out_data, size_t size);
esp_err_t firmware_crypto_cipher(const firmware_crypto_cipher_t *params,
                                firmware_crypto_span_t input, uint8_t *out_data);
esp_err_t firmware_crypto_aead(const firmware_crypto_aead_t *params,
                              firmware_crypto_span_t input, uint8_t *out_data);
esp_err_t firmware_crypto_x25519(const uint8_t private_key[32], const uint8_t peer[32],
                                uint8_t out_data[32]);
esp_err_t firmware_crypto_x25519_generate(uint8_t out_private_key[32], uint8_t out_public_key[32]);
void firmware_crypto_wipe(void *data, size_t size);

#ifdef __cplusplus
}
#endif
#endif

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

#include "nfc_felica.h"

#include <string.h>
#include "esp_log.h"
#include "protocols/common/legacy_des/des.h"

static const char *TAG = "NFC_FELICA_CRYPTO";

static void reverse(const uint8_t *in, uint8_t *out) {
  for (size_t i = 0; i < NFC_FELICA_HALF; i++)
    out[i] = in[NFC_FELICA_HALF - 1 - i];
}

esp_err_t
nfc_felica_crypto_init(nfc_felica_crypto_t *crypto, const uint8_t *key, const uint8_t *rc) {
  if (crypto == NULL || key == NULL || rc == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t reversed_key[NFC_FELICA_BYTES], reversed_rc[NFC_FELICA_BYTES];
  uint8_t iv[NFC_FELICA_HALF] = {0};
  for (size_t i = 0; i < NFC_FELICA_BYTES; i += NFC_FELICA_HALF) {
    reverse(key + i, reversed_key + i);
    reverse(rc + i, reversed_rc + i);
  }
  mbedtls_des3_context context;
  mbedtls_des3_init(&context);
  int error = mbedtls_des3_set2key_enc(&context, reversed_key);
  if (!error)
    error = mbedtls_des3_crypt_cbc(
        &context, MBEDTLS_DES_ENCRYPT, NFC_FELICA_BYTES, iv, reversed_rc, crypto->key);
  mbedtls_des3_free(&context);
  memset(reversed_key, 0, sizeof(reversed_key));
  if (error) {
    memset(crypto, 0, sizeof(*crypto));
    ESP_LOGE(TAG, "Session key failed");
    return ESP_FAIL;
  }
  memcpy(crypto->rc, rc, NFC_FELICA_BYTES);
  return ESP_OK;
}

static esp_err_t mac(const nfc_felica_crypto_t *crypto,
                     const uint8_t *first,
                     const nfc_felica_frame_t *data,
                     uint8_t *out) {
  uint8_t iv[NFC_FELICA_HALF], input[NFC_FELICA_HALF], encrypted[NFC_FELICA_HALF];
  reverse(crypto->rc, iv);
  reverse(first, input);
  mbedtls_des3_context context;
  mbedtls_des3_init(&context);
  int error = mbedtls_des3_set2key_enc(&context, crypto->key);
  for (size_t offset = 0; !error; offset += NFC_FELICA_HALF) {
    error = mbedtls_des3_crypt_cbc(
        &context, MBEDTLS_DES_ENCRYPT, NFC_FELICA_HALF, iv, input, encrypted);
    if (offset == data->size)
      break;
    reverse(data->data + offset, input);
  }
  mbedtls_des3_free(&context);
  if (error)
    return ESP_FAIL;
  reverse(encrypted, out);
  return ESP_OK;
}

esp_err_t nfc_felica_mac_read(const nfc_felica_crypto_t *crypto,
                              const uint8_t *blocks,
                              const nfc_felica_frame_t *data,
                              uint8_t *out) {
  if (!crypto || !blocks || !data || !out || data->size % NFC_FELICA_BYTES ||
      data->size > (NFC_FELICA_MAX_READ - 1) * NFC_FELICA_BYTES)
    return ESP_ERR_INVALID_ARG;
  uint8_t first[NFC_FELICA_HALF];
  memset(first, UINT8_MAX, sizeof(first));
  size_t count = data->size / NFC_FELICA_BYTES + 1;
  for (size_t i = 0; i < count; i++) {
    first[i * 2] = blocks[i];
    first[i * 2 + 1] = 0;
  }
  return mac(crypto, first, data, out);
}

esp_err_t nfc_felica_mac_write(const nfc_felica_crypto_t *crypto,
                               const uint8_t *counter,
                               const uint8_t *data,
                               uint8_t *out) {
  if (!crypto || !counter || !data || !out)
    return ESP_ERR_INVALID_ARG;
  uint8_t first[NFC_FELICA_HALF] = {
      counter[0], counter[1], counter[2], 0, NFC_FELICA_STATE, 0, NFC_FELICA_MAC_A, 0};
  nfc_felica_crypto_t swapped = *crypto;
  memcpy(swapped.key, crypto->key + NFC_FELICA_HALF, NFC_FELICA_HALF);
  memcpy(swapped.key + NFC_FELICA_HALF, crypto->key, NFC_FELICA_HALF);
  nfc_felica_frame_t frame = {.size = NFC_FELICA_BYTES};
  memcpy(frame.data, data, frame.size);
  esp_err_t error = mac(&swapped, first, &frame, out);
  memset(&swapped, 0, sizeof(swapped));
  return error;
}

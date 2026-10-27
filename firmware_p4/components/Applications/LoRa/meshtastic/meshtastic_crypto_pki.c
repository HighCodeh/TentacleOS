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

#include "meshtastic_crypto_pki.h"

#include "firmware_crypto.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "meshtastic_pki.h"

static const char *TAG = "MT_CRYPTO_PKI";

#define MT_PKI_AES_KEY_BITS 256
#define MT_PKI_NONCE_LEN    13
#define MT_PKI_TAG_LEN      8
#define MT_PKI_EXTRA_LEN    4

static bool compute_shared_key(const uint8_t *peer_pub, uint8_t shared_key[32]) {
  uint8_t raw_secret[FIRMWARE_CRYPTO_X25519_SIZE];
  esp_err_t error = firmware_crypto_x25519(mt_pki_get_privkey(), peer_pub, raw_secret);
  if (error == ESP_OK)
    error = firmware_crypto_sha256((firmware_crypto_span_t){raw_secret, sizeof(raw_secret)},
                                   shared_key);
  firmware_crypto_wipe(raw_secret, sizeof(raw_secret));
  return error == ESP_OK;
}

static void build_nonce(uint8_t nonce[MT_PKI_NONCE_LEN],
                        uint32_t from_node,
                        uint32_t packet_id,
                        uint32_t extra_nonce) {
  memset(nonce, 0, MT_PKI_NONCE_LEN);

  uint64_t packet_id_64 = (uint64_t)packet_id;
  memcpy(nonce, &packet_id_64, sizeof(uint64_t));

  memcpy(nonce + 8, &from_node, sizeof(uint32_t));

  if (extra_nonce != 0) {
    memcpy(nonce + 4, &extra_nonce, sizeof(uint32_t));
  }
}

bool mt_pki_encrypt_curve25519(const uint8_t *peer_pub,
                               uint32_t from_node,
                               uint32_t packet_id,
                               const uint8_t *plain,
                               size_t plen,
                               uint8_t *out) {
  uint8_t shared_key[32];
  if (!compute_shared_key(peer_pub, shared_key))
    return false;

  uint32_t extra_nonce = esp_random();
  if (extra_nonce == 0)
    extra_nonce = 1;

  uint8_t nonce[MT_PKI_NONCE_LEN];
  build_nonce(nonce, from_node, packet_id, extra_nonce);

  firmware_crypto_aead_t params = {
      .cipher = {.key = {shared_key, sizeof(shared_key)},
                 .iv = {nonce, sizeof(nonce)},
                 .algorithm = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, MT_PKI_TAG_LEN),
                 .is_encrypt = true},
      .tag = out + plen,
      .tag_size = MT_PKI_TAG_LEN};
  int rc = firmware_crypto_aead(&params, (firmware_crypto_span_t){plain, plen}, out);
  firmware_crypto_wipe(shared_key, sizeof(shared_key));

  if (rc != 0) {
    ESP_LOGW(TAG, "CCM encrypt failed rc=%d", rc);
    return false;
  }

  memcpy(out + plen + MT_PKI_TAG_LEN, &extra_nonce, MT_PKI_EXTRA_LEN);
  return true;
}

bool mt_pki_decrypt_curve25519(const uint8_t *peer_pub,
                               uint32_t from_node,
                               uint32_t packet_id,
                               const uint8_t *cipher,
                               size_t clen,
                               uint8_t *out) {
  if (clen <= MT_PKI_OVERHEAD)
    return false;

  size_t plen = clen - MT_PKI_OVERHEAD;
  const uint8_t *tag = cipher + plen;
  uint32_t extra_nonce;
  memcpy(&extra_nonce, cipher + plen + MT_PKI_TAG_LEN, MT_PKI_EXTRA_LEN);

  uint8_t shared_key[32];
  if (!compute_shared_key(peer_pub, shared_key))
    return false;

  uint8_t nonce[MT_PKI_NONCE_LEN];
  build_nonce(nonce, from_node, packet_id, extra_nonce);

  firmware_crypto_aead_t params = {
      .cipher = {.key = {shared_key, sizeof(shared_key)},
                 .iv = {nonce, sizeof(nonce)},
                 .algorithm = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, MT_PKI_TAG_LEN),
                 .is_encrypt = false},
      .tag = (uint8_t *)tag,
      .tag_size = MT_PKI_TAG_LEN};
  int rc = firmware_crypto_aead(&params, (firmware_crypto_span_t){cipher, plen}, out);
  firmware_crypto_wipe(shared_key, sizeof(shared_key));

  if (rc != 0) {
    ESP_LOGD(TAG, "CCM decrypt failed rc=%d (probably not PKI)", rc);
    return false;
  }
  return true;
}

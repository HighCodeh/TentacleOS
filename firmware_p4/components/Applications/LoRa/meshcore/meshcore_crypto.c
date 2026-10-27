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

#include "meshcore_internal.h"

#include "firmware_crypto.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "sodium.h"

static const char *TAG = "MESHCORE_CRYPTO";

static int
aes_ecb_encrypt_pad(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len);
static int aes_ecb_decrypt(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len);
static bool hmac_sha256_trunc(const uint8_t *key,
                              size_t key_len,
                              const uint8_t *data,
                              size_t data_len,
                              uint8_t *mac_out,
                              size_t mac_len);

esp_err_t meshcore_crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
  return firmware_crypto_sha256((firmware_crypto_span_t){data, len}, out);
}

int meshcore_crypto_encrypt_mac(const uint8_t *shared_secret,
                                uint8_t *dest,
                                const uint8_t *src,
                                int src_len) {
  int enc_len = aes_ecb_encrypt_pad(shared_secret, dest + MC_CIPHER_MAC_SIZE, src, src_len);
  if (enc_len == 0 || !hmac_sha256_trunc(shared_secret,
                                         MC_HMAC_KEY_SIZE,
                                         dest + MC_CIPHER_MAC_SIZE,
                                         enc_len,
                                         dest,
                                         MC_CIPHER_MAC_SIZE))
    return 0;
  return MC_CIPHER_MAC_SIZE + enc_len;
}

int meshcore_crypto_mac_decrypt(const uint8_t *shared_secret,
                                uint8_t *dest,
                                const uint8_t *src,
                                int src_len) {
  if (src_len < MC_CIPHER_MAC_SIZE + MC_CIPHER_BLOCK)
    return 0;
  int ct_len = src_len - MC_CIPHER_MAC_SIZE;

  uint8_t expected_mac[MC_CIPHER_MAC_SIZE];
  if (!hmac_sha256_trunc(shared_secret,
                         MC_HMAC_KEY_SIZE,
                         src + MC_CIPHER_MAC_SIZE,
                         ct_len,
                         expected_mac,
                         MC_CIPHER_MAC_SIZE))
    return 0;

  if (memcmp(expected_mac, src, MC_CIPHER_MAC_SIZE) != 0) {
    return 0;
  }

  return aes_ecb_decrypt(shared_secret, dest, src + MC_CIPHER_MAC_SIZE, ct_len);
}

bool meshcore_packet_parse(const uint8_t *raw, uint16_t raw_len, meshcore_packet_view_t *out) {
  if (raw_len < 2)
    return false;

  uint8_t hdr = raw[0];
  uint8_t version = (hdr >> 6) & 0x03;
  uint8_t payload_t = (hdr >> 2) & 0x0F;
  uint8_t route_t = hdr & 0x03;

  uint16_t offset = 1;
  if (route_t == MC_RT_TRANSPORT_FLOOD || route_t == MC_RT_TRANSPORT_DIRECT) {
    if (raw_len < offset + 4)
      return false;
    offset += 4;
  }

  if (raw_len < offset + 1)
    return false;
  uint8_t pl = raw[offset++];
  uint8_t hash_size_sel = (pl >> 6) & 0x03;
  uint8_t path_count = pl & 0x3F;
  uint8_t hash_size = mc_parse_hash_size(hash_size_sel);
  if (hash_size == 0)
    return false;

  uint16_t path_bytes = (uint16_t)path_count * hash_size;
  if (raw_len < offset + path_bytes)
    return false;

  out->version = version;
  out->payload_type = payload_t;
  out->route_type = route_t;
  out->hash_size = hash_size;
  out->path_count = path_count;
  out->path = &raw[offset];
  out->path_len = path_bytes;
  offset += path_bytes;
  out->payload = &raw[offset];
  out->payload_len = raw_len - offset;
  out->rssi_dbm = 0;
  out->snr_db = 0;
  return true;
}

uint16_t meshcore_packet_build_advert(const meshcore_identity_t *identity,
                                      int32_t lat_e6,
                                      int32_t lon_e6,
                                      bool has_latlon,
                                      uint32_t unix_ts,
                                      uint8_t *out,
                                      uint16_t out_cap) {
  uint8_t app_data[1 + 4 + 4 + MESHCORE_NAME_MAX];
  uint8_t app_len = 0;
  uint8_t flags = (identity->adv_type & 0x0F);
  if (has_latlon)
    flags |= MESHCORE_ADV_FLAG_HAS_LATLON;
  uint8_t name_len = (uint8_t)strlen(identity->name);
  if (name_len > 0 && name_len <= MESHCORE_NAME_MAX) {
    flags |= MESHCORE_ADV_FLAG_HAS_NAME;
  }
  app_data[app_len++] = flags;
  if (has_latlon) {
    memcpy(&app_data[app_len], &lat_e6, 4);
    app_len += 4;
    memcpy(&app_data[app_len], &lon_e6, 4);
    app_len += 4;
  }
  if (flags & MESHCORE_ADV_FLAG_HAS_NAME) {
    memcpy(&app_data[app_len], identity->name, name_len);
    app_len += name_len;
  }

  uint16_t total = 1 + 1 + MC_PUB_KEY_SIZE + 4 + MC_SIG_SIZE + app_len;
  if (total > out_cap)
    return 0;

  uint16_t pos = 0;
  out[pos++] = mc_build_header(MC_VERSION_V1, MC_PT_ADVERT, MC_RT_FLOOD);
  out[pos++] = mc_build_path_len(MC_HASH_SIZE_1B, 0);

  memcpy(&out[pos], identity->pub_key, MC_PUB_KEY_SIZE);
  pos += MC_PUB_KEY_SIZE;

  out[pos++] = (uint8_t)(unix_ts & 0xFF);
  out[pos++] = (uint8_t)((unix_ts >> 8) & 0xFF);
  out[pos++] = (uint8_t)((unix_ts >> 16) & 0xFF);
  out[pos++] = (uint8_t)((unix_ts >> 24) & 0xFF);

  uint16_t sig_pos = pos;
  pos += MC_SIG_SIZE;

  memcpy(&out[pos], app_data, app_len);
  pos += app_len;

  uint8_t sign_buf[32 + 4 + sizeof(app_data)];
  uint16_t sign_len = 32 + 4 + app_len;
  memcpy(&sign_buf[0], identity->pub_key, 32);
  memcpy(&sign_buf[32], &out[2 + 32], 4);
  memcpy(&sign_buf[36], app_data, app_len);

  crypto_sign_detached(&out[sig_pos], NULL, sign_buf, sign_len, identity->priv_key);

  return pos;
}

uint16_t meshcore_packet_build_grp_txt(uint8_t channel_hash,
                                       const uint8_t channel_secret[32],
                                       const char *sender_name,
                                       const char *text,
                                       uint32_t unix_ts,
                                       uint8_t *out,
                                       uint16_t out_cap) {
  uint8_t plaintext[160];
  uint16_t pt_len = 0;
  plaintext[pt_len++] = (uint8_t)(unix_ts & 0xFF);
  plaintext[pt_len++] = (uint8_t)((unix_ts >> 8) & 0xFF);
  plaintext[pt_len++] = (uint8_t)((unix_ts >> 16) & 0xFF);
  plaintext[pt_len++] = (uint8_t)((unix_ts >> 24) & 0xFF);
  plaintext[pt_len++] = 0x00;

  int n =
      snprintf((char *)&plaintext[pt_len], sizeof(plaintext) - pt_len, "%s: %s", sender_name, text);
  if (n < 0)
    return 0;
  pt_len += (uint16_t)n;

  uint16_t header_bytes = 1 + 1 + 1;
  if (header_bytes + MC_CIPHER_MAC_SIZE + ((pt_len + 15) & ~15) > out_cap)
    return 0;

  uint16_t pos = 0;
  out[pos++] = mc_build_header(MC_VERSION_V1, MC_PT_GRP_TXT, MC_RT_FLOOD);
  out[pos++] = mc_build_path_len(MC_HASH_SIZE_1B, 0);
  out[pos++] = channel_hash;

  int enc_written = meshcore_crypto_encrypt_mac(channel_secret, &out[pos], plaintext, pt_len);
  if (enc_written == 0)
    return 0;
  pos += enc_written;
  return pos;
}

esp_err_t meshcore_packet_hash(const meshcore_packet_view_t *pkt, uint8_t out[8]) {
  uint8_t buf[MESHCORE_MAX_RAW + 1];
  buf[0] = pkt->payload_type;
  if (pkt->payload_len > MESHCORE_MAX_RAW)
    return ESP_ERR_INVALID_SIZE;
  memcpy(&buf[1], pkt->payload, pkt->payload_len);
  uint8_t full[32];
  esp_err_t error = meshcore_crypto_sha256(buf, pkt->payload_len + 1, full);
  if (error == ESP_OK)
    memcpy(out, full, 8);
  return error;
}

uint8_t mc_build_header(uint8_t ver, uint8_t ptype, uint8_t route) {
  return ((ver & 0x03) << 6) | ((ptype & 0x0F) << 2) | (route & 0x03);
}

uint8_t mc_build_path_len(uint8_t hash_size_sel, uint8_t count) {
  return ((hash_size_sel & 0x03) << 6) | (count & 0x3F);
}

uint8_t mc_parse_hash_size(uint8_t sel) {
  switch (sel) {
    case 0:
      return 1;
    case 1:
      return 2;
    case 2:
      return 3;
    default:
      return 0;
  }
}

esp_err_t
mc_x25519(uint8_t out_shared[32], const uint8_t peer_pub_key[32], const uint8_t my_sk[64]) {
  if (out_shared == NULL || peer_pub_key == NULL || my_sk == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t curve_sk[crypto_scalarmult_SCALARBYTES];
  uint8_t curve_pk[crypto_scalarmult_BYTES];
  int rc = crypto_sign_ed25519_sk_to_curve25519(curve_sk, my_sk);
  if (rc == 0)
    rc = crypto_sign_ed25519_pk_to_curve25519(curve_pk, peer_pub_key);
  if (rc == 0)
    rc = crypto_scalarmult(out_shared, curve_sk, curve_pk);
  if (rc != 0) {
    ESP_LOGW(TAG, "Peer key agreement failed");
    sodium_memzero(out_shared, crypto_scalarmult_BYTES);
  }
  sodium_memzero(curve_sk, sizeof(curve_sk));
  sodium_memzero(curve_pk, sizeof(curve_pk));
  return rc == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t mc_sha256_two(
    uint8_t *out, size_t out_len, const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
  uint8_t full[FIRMWARE_CRYPTO_SHA256_SIZE];
  size_t written = 0;
  psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
  psa_status_t status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
  if (status == PSA_SUCCESS)
    status = psa_hash_update(&operation, a, a_len);
  if (status == PSA_SUCCESS)
    status = psa_hash_update(&operation, b, b_len);
  if (status == PSA_SUCCESS)
    status = psa_hash_finish(&operation, full, sizeof(full), &written);
  (void)psa_hash_abort(&operation);
  if (status == PSA_SUCCESS && written == sizeof(full) && out_len <= sizeof(full))
    memcpy(out, full, out_len);
  else {
    ESP_LOGW(TAG, "Hash failed");
    memset(out, 0, out_len);
  }
  firmware_crypto_wipe(full, sizeof(full));
  return status == PSA_SUCCESS && written == sizeof(full) && out_len <= sizeof(full) ? ESP_OK
                                                                                     : ESP_FAIL;
}

static int
aes_ecb_encrypt_pad(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len) {
  if (src_len <= 0 || src_len > MESHCORE_MAX_RAW)
    return 0;
  uint8_t padded[MESHCORE_MAX_RAW + FIRMWARE_CRYPTO_AES_BLOCK_SIZE] = {0};
  size_t size = ((size_t)src_len + FIRMWARE_CRYPTO_AES_BLOCK_SIZE - 1) &
                ~(FIRMWARE_CRYPTO_AES_BLOCK_SIZE - 1);
  memcpy(padded, src, src_len);
  firmware_crypto_cipher_t params = {.key = {key, FIRMWARE_CRYPTO_AES_BLOCK_SIZE},
                                     .algorithm = PSA_ALG_ECB_NO_PADDING,
                                     .is_encrypt = true};
  esp_err_t error = firmware_crypto_cipher(&params, (firmware_crypto_span_t){padded, size}, dest);
  firmware_crypto_wipe(padded, sizeof(padded));
  return error == ESP_OK ? (int)size : 0;
}

static int aes_ecb_decrypt(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len) {
  if (src_len <= 0 || src_len % FIRMWARE_CRYPTO_AES_BLOCK_SIZE != 0)
    return 0;
  firmware_crypto_cipher_t params = {.key = {key, FIRMWARE_CRYPTO_AES_BLOCK_SIZE},
                                     .algorithm = PSA_ALG_ECB_NO_PADDING,
                                     .is_encrypt = false};
  return firmware_crypto_cipher(&params, (firmware_crypto_span_t){src, (size_t)src_len}, dest) ==
                 ESP_OK
             ? src_len
             : 0;
}

static bool hmac_sha256_trunc(const uint8_t *key,
                              size_t key_len,
                              const uint8_t *data,
                              size_t data_len,
                              uint8_t *mac_out,
                              size_t mac_len) {
  uint8_t full_mac[FIRMWARE_CRYPTO_SHA256_SIZE];
  if (mac_len > sizeof(full_mac))
    return false;
  esp_err_t error = firmware_crypto_mac((firmware_crypto_span_t){key, key_len},
                                        (firmware_crypto_span_t){data, data_len},
                                        PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                        full_mac);
  if (error == ESP_OK)
    memcpy(mac_out, full_mac, mac_len);
  firmware_crypto_wipe(full_mac, sizeof(full_mac));
  return error == ESP_OK;
}

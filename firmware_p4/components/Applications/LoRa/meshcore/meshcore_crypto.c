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

#include "meshcore_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include "ed25519.h"

static int
aes_ecb_encrypt_pad(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len);
static int aes_ecb_decrypt(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len);
static void hmac_sha256_trunc(const uint8_t *key,
                              size_t key_len,
                              const uint8_t *data,
                              size_t data_len,
                              uint8_t *mac_out,
                              size_t mac_len);

void meshcore_crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, len);
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

int meshcore_crypto_encrypt_mac(const uint8_t *shared_secret,
                                uint8_t *dest,
                                const uint8_t *src,
                                int src_len) {
  int enc_len = aes_ecb_encrypt_pad(shared_secret, dest + MC_CIPHER_MAC_SIZE, src, src_len);
  hmac_sha256_trunc(shared_secret,
                    MC_HMAC_KEY_SIZE,
                    dest + MC_CIPHER_MAC_SIZE,
                    enc_len,
                    dest,
                    MC_CIPHER_MAC_SIZE);
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
  hmac_sha256_trunc(shared_secret,
                    MC_HMAC_KEY_SIZE,
                    src + MC_CIPHER_MAC_SIZE,
                    ct_len,
                    expected_mac,
                    MC_CIPHER_MAC_SIZE);

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

  ed25519_sign(&out[sig_pos], sign_buf, sign_len, identity->pub_key, identity->priv_key);

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
  pos += enc_written;
  return pos;
}

void meshcore_packet_hash(const meshcore_packet_view_t *pkt, uint8_t out[8]) {
  uint8_t buf[MESHCORE_MAX_RAW + 1];
  buf[0] = pkt->payload_type;
  if (pkt->payload_len > MESHCORE_MAX_RAW)
    return;
  memcpy(&buf[1], pkt->payload, pkt->payload_len);
  uint8_t full[32];
  meshcore_crypto_sha256(buf, pkt->payload_len + 1, full);
  memcpy(out, full, 8);
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

void mc_sha256_two(
    uint8_t *out, size_t out_len, const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
  uint8_t full[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, a, a_len);
  mbedtls_sha256_update(&ctx, b, b_len);
  mbedtls_sha256_finish(&ctx, full);
  mbedtls_sha256_free(&ctx);
  memcpy(out, full, out_len);
}

static int
aes_ecb_encrypt_pad(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);

  int out = 0;
  while (src_len >= 16) {
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, src, dest);
    src += 16;
    dest += 16;
    src_len -= 16;
    out += 16;
  }
  if (src_len > 0) {
    uint8_t tmp[16] = {0};
    memcpy(tmp, src, src_len);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, tmp, dest);
    out += 16;
  }
  mbedtls_aes_free(&ctx);
  return out;
}

static int aes_ecb_decrypt(const uint8_t key[16], uint8_t *dest, const uint8_t *src, int src_len) {
  if (src_len % 16 != 0)
    return 0;

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_dec(&ctx, key, 128);

  int out = 0;
  while (src_len >= 16) {
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, src, dest);
    src += 16;
    dest += 16;
    src_len -= 16;
    out += 16;
  }
  mbedtls_aes_free(&ctx);
  return out;
}

static void hmac_sha256_trunc(const uint8_t *key,
                              size_t key_len,
                              const uint8_t *data,
                              size_t data_len,
                              uint8_t *mac_out,
                              size_t mac_len) {
  uint8_t full_mac[32];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, key, key_len, data, data_len, full_mac);
  memcpy(mac_out, full_mac, mac_len);
}

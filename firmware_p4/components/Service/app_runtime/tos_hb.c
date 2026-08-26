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

#include "tos_hb.h"

#include <string.h>

#include "esp_log.h"
#include "sodium.h"

#include "tos_trust_keys.h"

static const char *TAG = "TOS_HB";

// Fixed header, little-endian:
//   0 magic "HBAP"(4) | 4 ver u16 | 6 flags u16 | 8 abi_major u16 |
//  10 abi_minor u16 | 12 caps u32 | 16 name[16] | 32 elf_len u32 | 36 reserved u32
#define HB_HDR_SIZE 40
#define HB_FORMAT_VER 2 // signed bundle (Ed25519 trailer)
#define HB_SIG_SIZE 64

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

esp_err_t tos_hb_open(const uint8_t *buf, size_t len, tos_hb_t *out) {
  if (buf == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  if (len < HB_HDR_SIZE) {
    ESP_LOGE(TAG, "too small (%u < %d)", (unsigned)len, HB_HDR_SIZE);
    return ESP_ERR_INVALID_SIZE;
  }
  if (memcmp(buf, "HBAP", 4) != 0) {
    ESP_LOGE(TAG, "bad magic");
    return ESP_ERR_NOT_SUPPORTED;
  }
  uint16_t ver = rd16(buf + 4);
  if (ver != HB_FORMAT_VER) {
    ESP_LOGE(TAG, "unsupported .hb format version %u", ver);
    return ESP_ERR_NOT_SUPPORTED;
  }

  uint32_t elf_len = rd32(buf + 32);
  size_t signed_len = (size_t)HB_HDR_SIZE + elf_len;
  if (signed_len + HB_SIG_SIZE > len) {
    ESP_LOGE(TAG, "bundle missing its %d-byte signature", HB_SIG_SIZE);
    return ESP_ERR_INVALID_SIZE;
  }

  // Verify the Ed25519 signature over (header || elf) against the trust store.
  const uint8_t *sig = buf + signed_len;
  if (sodium_init() < 0) {
    ESP_LOGE(TAG, "libsodium init failed");
    return ESP_FAIL;
  }
  bool trusted = false;
  for (int i = 0; i < TOS_TRUST_KEY_COUNT; i++) {
    if (crypto_sign_verify_detached(sig, buf, signed_len, TOS_TRUST_KEYS[i]) == 0) {
      trusted = true;
      break;
    }
  }
  if (!trusted) {
    ESP_LOGE(TAG, "signature invalid: untrusted key or tampered bundle");
    return ESP_ERR_INVALID_CRC;
  }

  out->abi_major = rd16(buf + 8);
  out->abi_minor = rd16(buf + 10);
  out->caps = rd32(buf + 12);
  memcpy(out->name, buf + 16, 15);
  out->name[15] = '\0';
  out->elf = buf + HB_HDR_SIZE;
  out->elf_len = elf_len;
  return ESP_OK;
}

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
//  10 abi_minor u16 | 12 caps u32 | 16 name[16] | 32 elf_len u32 | 36 meta_len u32
// v2: header || elf || sig(64)          (meta_len == 0, flags bit clear)
// v3: header || elf || meta || sig(64)  (meta_len set, HB_FLAG_HAS_META set)
// The Ed25519 signature covers everything before it, so a v3 meta block (title +
// icon) is inside the signed span and therefore trusted once verified.
#define HB_HDR_SIZE   40
#define HB_FORMAT_V2  2
#define HB_FORMAT_V3  3
#define HB_SIG_SIZE   64
#define HB_FLAG_HAS_META 0x0001u

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Parse the 40-byte header (magic/version/abi/caps/name) and return the ELF and
// meta lengths. No verification, no bounds check against a total length — the
// caller decides how many bytes it holds. @p hdr must be >= HB_HDR_SIZE bytes.
static esp_err_t parse_header(const uint8_t *hdr, tos_hb_t *out, uint32_t *elf_len,
                              uint32_t *meta_len) {
  if (memcmp(hdr, "HBAP", 4) != 0) {
    ESP_LOGE(TAG, "bad magic");
    return ESP_ERR_NOT_SUPPORTED;
  }
  uint16_t ver = rd16(hdr + 4);
  if (ver != HB_FORMAT_V2 && ver != HB_FORMAT_V3) {
    ESP_LOGE(TAG, "unsupported .hb format version %u", ver);
    return ESP_ERR_NOT_SUPPORTED;
  }
  uint16_t flags = rd16(hdr + 6);

  memset(out, 0, sizeof(*out));
  out->abi_major = rd16(hdr + 8);
  out->abi_minor = rd16(hdr + 10);
  out->caps = rd32(hdr + 12);
  memcpy(out->name, hdr + 16, 15);
  out->name[15] = '\0';

  *elf_len = rd32(hdr + 32);
  *meta_len = (ver == HB_FORMAT_V3 && (flags & HB_FLAG_HAS_META)) ? rd32(hdr + 36) : 0;
  return ESP_OK;
}

esp_err_t tos_hb_parse_meta(const uint8_t *meta, uint32_t meta_len, tos_hb_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;
  out->title = NULL;
  out->title_len = 0;
  out->icon = NULL;
  out->icon_len = 0;
  if (meta == NULL || meta_len == 0)
    return ESP_OK;

  // TLV records: [type u8][len u16 LE][bytes]. Unknown types are skipped.
  uint32_t off = 0;
  while (off + 3 <= meta_len) {
    uint8_t type = meta[off];
    uint16_t rlen = rd16(meta + off + 1);
    off += 3;
    if ((uint32_t)off + rlen > meta_len)
      break; // truncated record; stop
    if (type == TOS_HB_META_TITLE) {
      out->title = (const char *)(meta + off);
      out->title_len = rlen;
    } else if (type == TOS_HB_META_ICON) {
      out->icon = meta + off;
      out->icon_len = rlen;
    }
    off += rlen;
  }
  return ESP_OK;
}

esp_err_t tos_hb_read_header(const uint8_t *hdr, tos_hb_t *out, uint32_t *out_elf_len,
                             uint32_t *out_meta_len) {
  if (hdr == NULL || out == NULL || out_elf_len == NULL || out_meta_len == NULL)
    return ESP_ERR_INVALID_ARG;
  return parse_header(hdr, out, out_elf_len, out_meta_len);
}

esp_err_t tos_hb_open(const uint8_t *buf, size_t len, tos_hb_t *out) {
  if (buf == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  if (len < HB_HDR_SIZE) {
    ESP_LOGE(TAG, "too small (%u < %d)", (unsigned)len, HB_HDR_SIZE);
    return ESP_ERR_INVALID_SIZE;
  }

  uint32_t elf_len = 0, meta_len = 0;
  esp_err_t e = parse_header(buf, out, &elf_len, &meta_len);
  if (e != ESP_OK)
    return e;

  // Overflow-safe signed span = header + elf + meta (each addend fits in the
  // buffer; guard the sum against 32-bit size_t wrap before the range check).
  if (elf_len > len || meta_len > len) {
    ESP_LOGE(TAG, "elf/meta length runs past buffer");
    return ESP_ERR_INVALID_SIZE;
  }
  size_t signed_len = (size_t)HB_HDR_SIZE + elf_len + meta_len;
  if (signed_len < elf_len) { // wrap
    ESP_LOGE(TAG, "length overflow");
    return ESP_ERR_INVALID_SIZE;
  }
  if (signed_len + HB_SIG_SIZE > len) {
    ESP_LOGE(TAG, "bundle missing its %d-byte signature", HB_SIG_SIZE);
    return ESP_ERR_INVALID_SIZE;
  }

  // Verify the Ed25519 signature over the whole signed span against the trust
  // store. meta_len is folded in above, so title/icon are covered too.
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

  out->elf = buf + HB_HDR_SIZE;
  out->elf_len = elf_len;
  if (meta_len > 0)
    tos_hb_parse_meta(buf + HB_HDR_SIZE + elf_len, meta_len, out);
  return ESP_OK;
}

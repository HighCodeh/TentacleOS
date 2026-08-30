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

// Opener for the .hb app bundle (HighBoy App). The container carries a small
// fixed header then the raw ELF. Signature verification (Phase 5) is not here
// yet; for now this only parses and bounds-checks.

#ifndef TOS_HB_H
#define TOS_HB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define TOS_HB_HDR_SIZE 40 ///< fixed .hb header size, in bytes

/** Meta-block TLV record types (v3 `.hb`, the block after the ELF). */
#define TOS_HB_META_TITLE 1 ///< UTF-8 display title
#define TOS_HB_META_ICON  2 ///< LVGL .bin image (bin_header_t + pixels)

typedef struct {
  uint16_t abi_major;
  uint16_t abi_minor;
  uint32_t caps; ///< tos_cap_t bitmask the app requests
  char name[16]; ///< null-terminated app name (identity: grants/task/dedup key)
  const uint8_t *elf;
  size_t elf_len;
  const char *title;   ///< display title, NULL when absent (aliases into the buffer)
  uint16_t title_len;  ///< length of @ref title (no NUL guaranteed)
  const uint8_t *icon; ///< LVGL .bin image bytes, NULL when absent (aliases into the buffer)
  uint32_t icon_len;   ///< length of @ref icon
} tos_hb_t;

/**
 * @brief Parse, bounds-check and Ed25519-VERIFY a .hb bundle in @p buf.
 *
 * Accepts format v2 (header||elf||sig) and v3 (header||elf||meta||sig); the meta
 * block is inside the signed span, so a verified v3 bundle's title/icon are
 * trusted. On success fills @p out (elf/title/icon point into @p buf, not copies).
 * Fails on bad magic, unsupported version, a length that runs past @p buf, or an
 * invalid signature.
 */
esp_err_t tos_hb_open(const uint8_t *buf, size_t len, tos_hb_t *out);

/**
 * @brief Parse the 40-byte header only — NO signature verification.
 *
 * For cheap launcher scans: read @ref TOS_HB_HDR_SIZE bytes and call this to get
 * abi/caps/name plus the ELF and meta lengths, then seek to (hdr + elf_len) and
 * read @p out_meta_len bytes for @ref tos_hb_parse_meta. Metadata obtained this
 * way is UNVERIFIED (display-only); launch still goes through tos_hb_open.
 *
 * @param hdr           at least @ref TOS_HB_HDR_SIZE bytes.
 * @param out           filled with abi/caps/name (elf/title/icon left NULL/0).
 * @param out_elf_len   receives the ELF length.
 * @param out_meta_len  receives the meta-block length (0 for v2 / no meta).
 */
esp_err_t tos_hb_read_header(const uint8_t *hdr, tos_hb_t *out, uint32_t *out_elf_len,
                             uint32_t *out_meta_len);

/**
 * @brief Walk a meta TLV block, setting @p out->title / @p out->icon.
 *
 * Pointers alias into @p meta (copy them if they must outlive that buffer).
 * Unknown record types are skipped (forward-compat). Bounds-checked.
 */
esp_err_t tos_hb_parse_meta(const uint8_t *meta, uint32_t meta_len, tos_hb_t *out);

#ifdef __cplusplus
}
#endif

#endif // TOS_HB_H

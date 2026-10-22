// SPDX-License-Identifier: GPL-3.0-or-later
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

#ifndef HB_MUSE_TOKEN_FRAME_H
#define HB_MUSE_TOKEN_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HB_MUSE_TOKEN_MAX       1023u
#define HB_MUSE_TOKEN_HEADER    23u
#define HB_MUSE_TOKEN_FRAME_MAX (HB_MUSE_TOKEN_HEADER + HB_MUSE_TOKEN_MAX + 4u)
/* HBMU1 + 16-byte challenge + LE16 length + ASCII mgst_ token + LE32 CRC32.
 * Challenge prevents accidental stale upload; UART/physical access is trust.
 * CRC detects transfer errors, it is NOT authentication or encryption. */
#ifdef __cplusplus
extern "C" {
#endif

bool hb_muse_token_frame_decode(
    uint8_t *frame, size_t bytes, const uint8_t nonce[16], char **token, size_t *n);
#ifdef __cplusplus
}
#endif

#endif

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

#ifndef NFC_DES_SUPPORT_H
#define NFC_DES_SUPPORT_H

#include <stddef.h>
#include <stdint.h>
#ifdef TOS_DES_SELF_TEST
#include <stdio.h>
#endif

#define TOS_DES_C
#define DES_CHECK_RETURN            __attribute__((warn_unused_result))
#define DES_ERR_CORRUPTION_DETECTED -0x006E
#define TOS_DES_CBC
#define MBEDTLS_GET_UINT32_BE(b, i)                                                            \
  (((uint32_t)(b)[i] << 24) | ((uint32_t)(b)[(i) + 1] << 16) | ((uint32_t)(b)[(i) + 2] << 8) | \
   (uint32_t)(b)[(i) + 3])
#define MBEDTLS_PUT_UINT32_BE(n, b, i)   \
  do {                                   \
    (b)[i] = (uint8_t)((n) >> 24);       \
    (b)[(i) + 1] = (uint8_t)((n) >> 16); \
    (b)[(i) + 2] = (uint8_t)((n) >> 8);  \
    (b)[(i) + 3] = (uint8_t)(n);         \
  } while (0)

static inline void des_zeroize(void *buffer, size_t length) {
  volatile uint8_t *bytes = buffer;
  while (length-- > 0)
    *bytes++ = 0;
}

static inline void des_xor(uint8_t *out, const uint8_t *a, const uint8_t *b, size_t length) {
  for (size_t i = 0; i < length; ++i)
    out[i] = a[i] ^ b[i];
}

#endif

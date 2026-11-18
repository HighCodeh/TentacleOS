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

#include "crypto1.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#define DRAM_ATTR
#endif

#define SWAPENDIAN(x) \
  ((x) = ((x) >> 8 & 0xff00ffU) | ((x) & 0xff00ffU) << 8, (x) = (x) >> 16 | (x) << 16)

#define BIT(x, n)   (((x) >> (n)) & 1U)
#define BEBIT(x, n) BIT((x), (n) ^ 24)

#define LF_POLY_ODD  (0x29CE5CU)
#define LF_POLY_EVEN (0x870804U)

#define CRYPTO1_KEY_BITS      48
#define CRYPTO1_KEY_BIT_STEP  2
#define CRYPTO1_BITS_PER_BYTE 8
#define CRYPTO1_WORD_BITS     32
#define CRYPTO1_BEBIT_SWAP    24

static const DRAM_ATTR uint8_t s_odd_parity_table[256] = {
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1};

uint8_t IRAM_ATTR crypto1_odd_parity8(uint8_t data) {
  return s_odd_parity_table[data];
}

uint8_t IRAM_ATTR crypto1_even_parity32(uint32_t data) {
  data ^= data >> 16;
  data ^= data >> 8;
  return (uint8_t)(!s_odd_parity_table[data & 0xFF]);
}

static uint32_t IRAM_ATTR filter(uint32_t in) {
  uint32_t out = 0;
  out = 0xf22c0U >> (in & 0xfU) & 16U;
  out |= 0x6c9c0U >> (in >> 4 & 0xfU) & 8U;
  out |= 0x3c8b0U >> (in >> 8 & 0xfU) & 4U;
  out |= 0x1e458U >> (in >> 12 & 0xfU) & 2U;
  out |= 0x0d938U >> (in >> 16 & 0xfU) & 1U;
  return BIT(0xEC57E80AU, out);
}

void IRAM_ATTR crypto1_init(crypto1_state_t *s, uint64_t key) {
  s->even = 0;
  s->odd = 0;
  for (int8_t i = CRYPTO1_KEY_BITS - 1; i > 0; i -= CRYPTO1_KEY_BIT_STEP) {
    s->odd = s->odd << 1 | BIT(key, (unsigned)((i - 1) ^ 7));
    s->even = s->even << 1 | BIT(key, (unsigned)(i ^ 7));
  }
}

void IRAM_ATTR crypto1_reset(crypto1_state_t *s) {
  s->odd = 0;
  s->even = 0;
}

uint8_t IRAM_ATTR crypto1_bit(crypto1_state_t *s, uint8_t in, int is_encrypted) {
  uint8_t out = (uint8_t)filter(s->odd);
  uint32_t feed = out & (uint32_t)(!!is_encrypted);
  feed ^= (uint32_t)(!!in);
  feed ^= LF_POLY_ODD & s->odd;
  feed ^= LF_POLY_EVEN & s->even;
  s->even = s->even << 1 | crypto1_even_parity32(feed);

  uint32_t tmp = s->odd;
  s->odd = s->even;
  s->even = tmp;

  return out;
}

uint8_t IRAM_ATTR crypto1_byte(crypto1_state_t *s, uint8_t in, int is_encrypted) {
  uint8_t out = 0;
  for (uint8_t i = 0; i < CRYPTO1_BITS_PER_BYTE; i++) {
    out |= (uint8_t)(crypto1_bit(s, BIT(in, i), is_encrypted) << i);
  }
  return out;
}

uint32_t IRAM_ATTR crypto1_word(crypto1_state_t *s, uint32_t in, int is_encrypted) {
  uint32_t out = 0;
  for (uint8_t i = 0; i < CRYPTO1_WORD_BITS; i++) {
    out |= (uint32_t)crypto1_bit(s, BEBIT(in, i), is_encrypted) << (CRYPTO1_BEBIT_SWAP ^ i);
  }
  return out;
}

uint8_t IRAM_ATTR crypto1_filter_output(crypto1_state_t *s) {
  return (uint8_t)filter(s->odd);
}

uint32_t IRAM_ATTR crypto1_prng_successor(uint32_t x, uint32_t n) {
  SWAPENDIAN(x);
  while (n--)
    x = x >> 1 | (x >> 16 ^ x >> 18 ^ x >> 19 ^ x >> 21) << 31;
  return SWAPENDIAN(x);
}

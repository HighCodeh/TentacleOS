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
/**
 * @file crypto1.h
 * @brief Crypto1 cipher for MIFARE Classic.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t odd;
    uint32_t even;
} crypto1_state_t;

/** Initialise the LFSR with a 48-bit key. */
void    crypto1_init(crypto1_state_t *s, uint64_t key);

/** Reset LFSR state to zero. */
void    crypto1_reset(crypto1_state_t *s);

/**
 * Clock one bit through the cipher.
 *
 * @param s Cipher state.
 * @param in Input bit.
 * @param is_encrypted 0 = TX / priming, 1 = RX (encrypted input).
 * @return Keystream bit.
 */
uint8_t crypto1_bit(crypto1_state_t *s, uint8_t in, int is_encrypted);

/** Clock one byte (8 bits, LSB first). Returns keystream byte. */
uint8_t crypto1_byte(crypto1_state_t *s, uint8_t in, int is_encrypted);

/**
 * Clock one 32-bit word through the cipher.
 * Returns keystream word in BEBIT order.
 */
uint32_t crypto1_word(crypto1_state_t *s, uint32_t in, int is_encrypted);

/**
 * Get the current filter output WITHOUT advancing the LFSR.
 * Used for parity bit encryption (the 9th keystream bit per byte).
 */
uint8_t crypto1_filter_output(crypto1_state_t *s);

/**
 * MIFARE Classic card PRNG with proper SWAPENDIAN.
 * Input and output are in big-endian byte order (as received on wire).
 */
uint32_t crypto1_prng_successor(uint32_t x, uint32_t n);

/** Odd parity of a byte (1 if odd number of set bits). */
uint8_t crypto1_odd_parity8(uint8_t data);

/** Even parity of a 32-bit word. */
uint8_t crypto1_even_parity32(uint32_t data);

#ifdef __cplusplus
}
#endif

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
#ifndef CRYPTO1_H
#define CRYPTO1_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Crypto1 LFSR state.
 */
typedef struct {
  uint32_t odd;
  uint32_t even;
} crypto1_state_t;

/**
 * @brief Initialise the LFSR with a 48-bit key.
 *
 * @param s    Cipher state to initialise.
 * @param key  48-bit key value.
 */
void crypto1_init(crypto1_state_t *s, uint64_t key);

/**
 * @brief Reset LFSR state to zero.
 *
 * @param s  Cipher state to reset.
 */
void crypto1_reset(crypto1_state_t *s);

/**
 * @brief Clock one bit through the cipher.
 *
 * @param s             Cipher state.
 * @param in            Input bit.
 * @param is_encrypted  0 = TX / priming, 1 = RX (encrypted input).
 * @return Keystream bit.
 */
uint8_t crypto1_bit(crypto1_state_t *s, uint8_t in, int is_encrypted);

/**
 * @brief Clock one byte (8 bits, LSB first).
 *
 * @param s             Cipher state.
 * @param in            Input byte.
 * @param is_encrypted  0 = TX / priming, 1 = RX (encrypted input).
 * @return Keystream byte.
 */
uint8_t crypto1_byte(crypto1_state_t *s, uint8_t in, int is_encrypted);

/**
 * @brief Clock one 32-bit word through the cipher.
 *
 * Returns keystream word in BEBIT order.
 *
 * @param s             Cipher state.
 * @param in            Input word.
 * @param is_encrypted  0 = TX / priming, 1 = RX (encrypted input).
 * @return Keystream word in BEBIT order.
 */
uint32_t crypto1_word(crypto1_state_t *s, uint32_t in, int is_encrypted);

/**
 * @brief Get the current filter output without advancing the LFSR.
 *
 * Used for parity bit encryption (the 9th keystream bit per byte).
 *
 * @param s  Cipher state.
 * @return Filter output bit.
 */
uint8_t crypto1_filter_output(crypto1_state_t *s);

/**
 * @brief Compute MIFARE Classic card PRNG successor.
 *
 * Input and output are in big-endian byte order (as received on wire).
 *
 * @param x  Current PRNG state.
 * @param n  Number of successor steps.
 * @return PRNG state after n steps.
 */
uint32_t crypto1_prng_successor(uint32_t x, uint32_t n);

/**
 * @brief Compute odd parity of a byte.
 *
 * @param data  Input byte.
 * @return 1 if odd number of set bits, 0 otherwise.
 */
uint8_t crypto1_odd_parity8(uint8_t data);

/**
 * @brief Compute even parity of a 32-bit word.
 *
 * @param data  Input word.
 * @return Even parity bit.
 */
uint8_t crypto1_even_parity32(uint32_t data);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO1_H */

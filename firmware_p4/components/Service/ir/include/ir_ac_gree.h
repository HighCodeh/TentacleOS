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

#ifndef IR_AC_GREE_H
#define IR_AC_GREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_ac.h"

/** @brief Gree carrier frequency in Hz. */
#define GREE_CARRIER_HZ 38000

/** @brief Gree header mark duration in microseconds. */
#define GREE_HDR_MARK 9000

/** @brief Gree header space duration in microseconds. */
#define GREE_HDR_SPACE 4500

/** @brief Gree bit mark duration in microseconds. */
#define GREE_BIT_MARK 620

/** @brief Gree one-bit space duration in microseconds. */
#define GREE_ONE_SPACE 1600

/** @brief Gree zero-bit space duration in microseconds. */
#define GREE_ZERO_SPACE 540

/** @brief Gree gap between the two 32-bit blocks, in microseconds. */
#define GREE_MSG_SPACE 19000

/** @brief Number of state bytes in a Gree frame. */
#define GREE_STATE_LEN 8

/** @brief 3-bit block separator transmitted between the two halves. */
#define GREE_BLOCK_FOOTER 0b010

/** @brief Number of bits in the block separator. */
#define GREE_BLOCK_FOOTER_BITS 3

/**
 * @brief Encode a Gree AC state into RMT symbols.
 *
 * Builds the 8-byte state, computes the Kelvinator block checksum, and emits
 * two 32-bit blocks separated by a 3-bit footer and a long gap.
 *
 * @param[in]  state    AC state to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_ac_gree_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Decode RMT symbols into a Gree AC state.
 *
 * Validates the header and the Kelvinator block checksum before extracting
 * power, mode, temperature and fan.
 *
 * @param[in]  symbols    RMT symbol buffer. Must not be NULL.
 * @param[in]  count      Number of symbols.
 * @param[out] out_state  Destination for the decoded state. Must not be NULL.
 *
 * @return true if the frame is a valid Gree state, false otherwise.
 */
bool ir_ac_gree_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // IR_AC_GREE_H

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

#ifndef IR_AC_MIDEA_H
#define IR_AC_MIDEA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_ac.h"

/** @brief Midea carrier frequency in Hz. */
#define MIDEA_CARRIER_HZ 38000

/** @brief Midea header mark duration in microseconds. */
#define MIDEA_HDR_MARK 4480

/** @brief Midea header space duration in microseconds. */
#define MIDEA_HDR_SPACE 4480

/** @brief Midea bit mark duration in microseconds. */
#define MIDEA_BIT_MARK 560

/** @brief Midea one-bit space duration in microseconds. */
#define MIDEA_ONE_SPACE 1680

/** @brief Midea zero-bit space duration in microseconds. */
#define MIDEA_ZERO_SPACE 560

/** @brief Midea gap between the normal and inverted sub-frames, in microseconds. */
#define MIDEA_MIN_GAP 4240

/** @brief Number of data bits in a Midea sub-frame. */
#define MIDEA_FRAME_BITS 48

/**
 * @brief Encode a Midea AC state into RMT symbols.
 *
 * Emits the 48-bit frame MSB-first followed by a fully inverted copy, as the
 * Midea protocol requires.
 *
 * @param[in]  state    AC state to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_ac_midea_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Decode RMT symbols into a Midea AC state.
 *
 * Validates the header field and the bit-reversed nibble checksum before
 * extracting power, mode, temperature and fan.
 *
 * @param[in]  symbols    RMT symbol buffer. Must not be NULL.
 * @param[in]  count      Number of symbols.
 * @param[out] out_state  Destination for the decoded state. Must not be NULL.
 *
 * @return true if the frame is a valid Midea state, false otherwise.
 */
bool ir_ac_midea_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // IR_AC_MIDEA_H

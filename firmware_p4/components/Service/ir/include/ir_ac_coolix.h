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

#ifndef IR_AC_COOLIX_H
#define IR_AC_COOLIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_ac.h"

/** @brief Coolix carrier frequency in Hz. */
#define COOLIX_CARRIER_HZ 38000

/** @brief Coolix header mark duration in microseconds. */
#define COOLIX_HDR_MARK 4692

/** @brief Coolix header space duration in microseconds. */
#define COOLIX_HDR_SPACE 4416

/** @brief Coolix bit mark duration in microseconds. */
#define COOLIX_BIT_MARK 552

/** @brief Coolix one-bit space duration in microseconds. */
#define COOLIX_ONE_SPACE 1656

/** @brief Coolix zero-bit space duration in microseconds. */
#define COOLIX_ZERO_SPACE 552

/**
 * @brief Encode a Coolix AC state into RMT symbols.
 *
 * The 24-bit state is transmitted MSB-first as three bytes, each byte followed
 * by its bitwise complement.
 *
 * @param[in]  state    AC state to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_ac_coolix_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Decode RMT symbols into a Coolix AC state.
 *
 * Validates the header and the three byte/complement pairs before extracting
 * power, mode, temperature and fan.
 *
 * @param[in]  symbols    RMT symbol buffer. Must not be NULL.
 * @param[in]  count      Number of symbols.
 * @param[out] out_state  Destination for the decoded state. Must not be NULL.
 *
 * @return true if the frame is a valid Coolix state, false otherwise.
 */
bool ir_ac_coolix_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // IR_AC_COOLIX_H

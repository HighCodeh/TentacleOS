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

#ifndef IR_AC_TOSHIBA_H
#define IR_AC_TOSHIBA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_ac.h"

/** @brief Toshiba carrier frequency in Hz. */
#define TOSHIBA_CARRIER_HZ 38000

/** @brief Toshiba header mark duration in microseconds. */
#define TOSHIBA_HDR_MARK 4400

/** @brief Toshiba header space duration in microseconds. */
#define TOSHIBA_HDR_SPACE 4300

/** @brief Toshiba bit mark duration in microseconds. */
#define TOSHIBA_BIT_MARK 580

/** @brief Toshiba one-bit space duration in microseconds. */
#define TOSHIBA_ONE_SPACE 1600

/** @brief Toshiba zero-bit space duration in microseconds. */
#define TOSHIBA_ZERO_SPACE 490

/** @brief Toshiba trailing gap duration in microseconds. */
#define TOSHIBA_MIN_GAP 4600

/** @brief Number of state bytes in a Toshiba frame. */
#define TOSHIBA_STATE_LEN 9

/** @brief Number of data bits in a Toshiba frame. */
#define TOSHIBA_FRAME_BITS 72

/**
 * @brief Encode a Toshiba AC state into RMT symbols.
 *
 * 9-byte frame, MSB-first, with a trailing XOR checksum byte.
 *
 * @param[in]  state    AC state to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_ac_toshiba_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Decode RMT symbols into a Toshiba AC state.
 *
 * Validates the fixed header bytes and the XOR checksum before extracting
 * mode, temperature and fan.
 *
 * @param[in]  symbols    RMT symbol buffer. Must not be NULL.
 * @param[in]  count      Number of symbols.
 * @param[out] out_state  Destination for the decoded state. Must not be NULL.
 *
 * @return true if the frame is a valid Toshiba state, false otherwise.
 */
bool ir_ac_toshiba_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // IR_AC_TOSHIBA_H

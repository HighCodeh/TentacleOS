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

#ifndef IR_AC_HAIER_H
#define IR_AC_HAIER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_ac.h"

/** @brief Haier carrier frequency in Hz. */
#define HAIER_CARRIER_HZ 38000

/** @brief Haier pre-header mark duration in microseconds. */
#define HAIER_PRE_MARK 3000

/** @brief Haier pre-header space duration in microseconds. */
#define HAIER_PRE_SPACE 3000

/** @brief Haier header mark duration in microseconds. */
#define HAIER_HDR_MARK 3000

/** @brief Haier header gap (space after the second header mark) in microseconds. */
#define HAIER_HDR_GAP 4300

/** @brief Haier bit mark duration in microseconds. */
#define HAIER_BIT_MARK 520

/** @brief Haier one-bit space duration in microseconds. */
#define HAIER_ONE_SPACE 1650

/** @brief Haier zero-bit space duration in microseconds. */
#define HAIER_ZERO_SPACE 650

/** @brief Number of state bytes in a Haier frame. */
#define HAIER_STATE_LEN 9

/** @brief Number of data bits in a Haier frame. */
#define HAIER_FRAME_BITS 72

/**
 * @brief Encode a Haier AC state into RMT symbols.
 *
 * 9-byte frame, MSB-first, preceded by a double header and ending with a
 * byte-sum checksum.
 *
 * @param[in]  state    AC state to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_ac_haier_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Decode RMT symbols into a Haier AC state.
 *
 * Validates the double header, the 0xA5 prefix and the byte-sum checksum
 * before extracting power, mode, temperature and fan.
 *
 * @param[in]  symbols    RMT symbol buffer. Must not be NULL.
 * @param[in]  count      Number of symbols.
 * @param[out] out_state  Destination for the decoded state. Must not be NULL.
 *
 * @return true if the frame is a valid Haier state, false otherwise.
 */
bool ir_ac_haier_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // IR_AC_HAIER_H

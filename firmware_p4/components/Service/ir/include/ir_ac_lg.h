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

#ifndef IR_AC_LG_H
#define IR_AC_LG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_ac.h"

/** @brief LG AC carrier frequency in Hz. */
#define LGAC_CARRIER_HZ 38000

/** @brief LG AC header mark duration in microseconds. */
#define LGAC_HDR_MARK 8500

/** @brief LG AC header space duration in microseconds. */
#define LGAC_HDR_SPACE 4250

/** @brief LG AC bit mark duration in microseconds. */
#define LGAC_BIT_MARK 550

/** @brief LG AC one-bit space duration in microseconds. */
#define LGAC_ONE_SPACE 1600

/** @brief LG AC zero-bit space duration in microseconds. */
#define LGAC_ZERO_SPACE 550

/** @brief Number of data bits in an LG AC frame. */
#define LGAC_FRAME_BITS 28

/**
 * @brief Encode an LG AC state into RMT symbols.
 *
 * 28-bit MSB-first frame: 0x88 signature, power, mode, temperature, fan and a
 * 4-bit nibble-sum checksum.
 *
 * @param[in]  state    AC state to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_ac_lg_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Decode RMT symbols into an LG AC state.
 *
 * Validates the header, the 0x88 signature and the nibble-sum checksum before
 * extracting power, mode, temperature and fan.
 *
 * @param[in]  symbols    RMT symbol buffer. Must not be NULL.
 * @param[in]  count      Number of symbols.
 * @param[out] out_state  Destination for the decoded state. Must not be NULL.
 *
 * @return true if the frame is a valid LG AC state, false otherwise.
 */
bool ir_ac_lg_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // IR_AC_LG_H

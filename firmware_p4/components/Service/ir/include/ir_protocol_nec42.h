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

#ifndef IR_PROTOCOL_NEC42_H
#define IR_PROTOCOL_NEC42_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_protocol.h"

/** @brief Number of data bits in a NEC42 frame (13+13 address, 8+8 command). */
#define NEC42_FRAME_BITS 42

/** @brief Bit mask for the 13-bit NEC42 address field. */
#define NEC42_ADDR_MASK 0x1FFF

/** @brief Bit mask for the 8-bit NEC42 command field. */
#define NEC42_CMD_MASK 0xFF

/** @brief Bit position of the inverted address field in the NEC42 frame word. */
#define NEC42_ADDR_INV_SHIFT 13

/** @brief Bit position of the command field in the NEC42 frame word. */
#define NEC42_CMD_SHIFT 26

/** @brief Bit position of the inverted command field in the NEC42 frame word. */
#define NEC42_CMD_INV_SHIFT 34

/** @brief Minimum number of RMT symbols for a valid NEC42 frame. */
#define NEC42_MIN_SYMBOLS 43

/**
 * @brief Decode a NEC42 IR frame from RMT symbols.
 *
 * 42-bit frame with a 13-bit address and 8-bit command, each followed by its
 * bitwise complement. Shares NEC timing but is decoded before NEC so it is not
 * truncated to 32 bits.
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a valid NEC42 frame was decoded, false otherwise.
 */
bool ir_protocol_nec42_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data);

/**
 * @brief Encode a NEC42 IR command into RMT symbols.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_protocol_nec42_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_NEC42_H

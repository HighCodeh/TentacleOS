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

#ifndef IR_PROTOCOL_NEC_H
#define IR_PROTOCOL_NEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_protocol.h"

/** @brief NEC header mark duration in microseconds. */
#define NEC_HEADER_MARK 9000

/** @brief NEC header space duration in microseconds. */
#define NEC_HEADER_SPACE 4500

/** @brief NEC bit mark duration in microseconds. */
#define NEC_BIT_MARK 560

/** @brief NEC one-bit space duration in microseconds. */
#define NEC_ONE_SPACE 1690

/** @brief NEC zero-bit space duration in microseconds. */
#define NEC_ZERO_SPACE 560

/** @brief NEC repeat frame header space duration in microseconds. */
#define NEC_REPEAT_SPACE 2250

/** @brief Number of data bits in a full NEC frame. */
#define NEC_FRAME_BITS 32

/** @brief Minimum number of RMT symbols for a valid full NEC frame. */
#define NEC_MIN_SYMBOLS 34

/** @brief Maximum number of RMT symbols used to identify a repeat frame. */
#define NEC_REPEAT_MAX_SYMBOLS 4

/** @brief Number of RMT symbols in an encoded NEC repeat frame. */
#define NEC_REPEAT_SYMBOL_COUNT 2

/** @brief Bit position of the inverted address field in the NEC frame word. */
#define NEC_ADDR_INV_SHIFT 8

/** @brief Bit position of the command field in the NEC frame word. */
#define NEC_CMD_SHIFT 16

/** @brief Bit position of the inverted command field in the NEC frame word. */
#define NEC_CMD_INV_SHIFT 24

/** @brief XOR result expected when a byte and its complement are combined. */
#define NEC_INTEGRITY_MASK 0xFF

/** @brief Maximum address value for a standard NEC frame (8-bit device). */
#define NEC_ADDR_STANDARD_MAX 0xFF

/** @brief Bit mask for the 16-bit extended address field in a NECext frame. */
#define NEC_EXT_ADDR_MASK 0xFFFF

/** @brief Bit position of the high command byte in a 16-bit extended command (ONKYO/Apple). */
#define NEC_CMD_HI_SHIFT 8

/** @brief Bit mask for the 16-bit extended command field. */
#define NEC_EXT_CMD_MASK 0xFFFF

/**
 * @brief Decode a NEC IR frame from RMT symbols.
 *
 * Detects both full frames and repeat frames. For repeat frames, only
 * out_data->protocol and out_data->repeat are set. Supports standard NEC
 * (8-bit address with inverse) and extended NEC (16-bit address).
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a valid NEC frame was decoded, false otherwise.
 */
bool ir_protocol_nec_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data);

/**
 * @brief Encode a NEC IR command into RMT symbols.
 *
 * If data->repeat is true, encodes a repeat frame instead of a full frame.
 * Selects standard (8-bit) or extended (16-bit) address format based on address range.
 * Appends inverted command byte for integrity.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_protocol_nec_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_NEC_H
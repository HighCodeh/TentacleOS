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

#ifndef IR_PROTOCOL_LG_H
#define IR_PROTOCOL_LG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_protocol.h"

/** @brief LG header mark duration in microseconds. */
#define LG_HEADER_MARK 8416

/** @brief LG header space duration in microseconds. */
#define LG_HEADER_SPACE 4208

/** @brief LG bit mark duration in microseconds. */
#define LG_BIT_MARK 526

/** @brief LG one-bit space duration in microseconds. */
#define LG_ONE_SPACE 1578

/** @brief LG zero-bit space duration in microseconds. */
#define LG_ZERO_SPACE 550

/** @brief LG repeat frame header space duration in microseconds. */
#define LG_REPEAT_SPACE 2104

/** @brief Number of data bits in a full LG frame. */
#define LG_FRAME_BITS 28

/** @brief Minimum number of RMT symbols for a valid full LG frame. */
#define LG_MIN_SYMBOLS 30

/** @brief Maximum number of RMT symbols used to identify a repeat frame. */
#define LG_REPEAT_MAX_SYMBOLS 4

/** @brief Number of RMT symbols in an encoded LG repeat frame. */
#define LG_REPEAT_SYMBOL_COUNT 2

/** @brief Number of nibbles in a 16-bit command word used by the checksum. */
#define LG_CHECKSUM_NIBBLES 4

/** @brief Bit shift between consecutive nibbles (bits per nibble). */
#define LG_NIBBLE_SHIFT 4

/** @brief Mask to extract a single nibble. */
#define LG_NIBBLE_MASK 0xF

/** @brief Bit position of the address field in the LG frame word. */
#define LG_ADDR_SHIFT 20

/** @brief Bit position of the command field in the LG frame word. */
#define LG_CMD_SHIFT 4

/** @brief Bit mask for the 8-bit address field in the LG frame word. */
#define LG_ADDR_MASK 0xFF

/** @brief Bit mask for the 16-bit command field in the LG frame word. */
#define LG_CMD_MASK 0xFFFF

/** @brief Address byte that marks an LG air-conditioner frame, decoded by the AC layer. */
#define LG_AC_SIGNATURE 0x88

/**
 * @brief Decode an LG IR frame from RMT symbols.
 *
 * Detects both full frames and repeat frames. For repeat frames, only
 * out_data->protocol and out_data->repeat are set. Validates the 4-bit
 * checksum embedded in the frame.
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a valid LG frame was decoded, false otherwise.
 */
bool ir_protocol_lg_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data);

/**
 * @brief Encode an LG IR command into RMT symbols.
 *
 * If data->repeat is true, encodes a repeat frame instead of a full frame.
 * Computes and appends the 4-bit checksum from the command field.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_protocol_lg_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_LG_H
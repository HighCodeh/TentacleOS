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

#ifndef IR_PROTOCOL_DENON_H
#define IR_PROTOCOL_DENON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_protocol.h"

/** @brief Denon bit mark duration in microseconds. */
#define DENON_BIT_MARK 260

/** @brief Denon one-bit space duration in microseconds. */
#define DENON_ONE_SPACE 1820

/** @brief Denon zero-bit space duration in microseconds. */
#define DENON_ZERO_SPACE 780

/** @brief Number of data bits in a Denon frame. */
#define DENON_FRAME_BITS 15

/** @brief Minimum number of RMT symbols for a valid Denon frame. */
#define DENON_MIN_SYMBOLS 16

/** @brief Maximum acceptable mark duration in microseconds for frame detection. */
#define DENON_MARK_MAX_US 600

/** @brief Number of leading marks validated to confirm a Denon frame. */
#define DENON_VALIDATE_MARK_COUNT 3

/** @brief Bit mask for the 5-bit address field in the Denon frame. */
#define DENON_ADDR_MASK 0x1F

/** @brief Bit position of the command field in the Denon frame word. */
#define DENON_CMD_SHIFT 5

/** @brief Bit position of the 2-bit frame-type field in the Denon frame word. */
#define DENON_FRAME_SHIFT 13

/** @brief Bit mask for the 2-bit frame-type field. */
#define DENON_FRAME_MASK 0x3

/** @brief Frame-type value of the complementary auto-repeat frame (inverted). */
#define DENON_FRAME_INVERTED 0x3

/** @brief XOR mask that inverts the command and frame-type bits for the second frame. */
#define DENON_INVERT_MASK 0x7FE0

/** @brief Silence between the normal frame and its complementary repeat, in microseconds. */
#define DENON_REPEAT_GAP_US 45000

/**
 * @brief Decode a Denon IR frame from RMT symbols.
 *
 * Denon uses no header — frame detection relies on validating the leading
 * bit marks against DENON_BIT_MARK.
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a valid Denon frame was decoded, false otherwise.
 */
bool ir_protocol_denon_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data);

/**
 * @brief Encode a Denon IR command into RMT symbols.
 *
 * Denon uses no header — the frame starts directly with the first bit mark.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_protocol_denon_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_DENON_H
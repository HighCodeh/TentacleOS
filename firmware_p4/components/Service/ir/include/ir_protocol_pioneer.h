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

#ifndef IR_PROTOCOL_PIONEER_H
#define IR_PROTOCOL_PIONEER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_protocol.h"

/** @brief Pioneer header mark duration in microseconds. */
#define PIONEER_HEADER_MARK 8500

/** @brief Pioneer header space duration in microseconds. */
#define PIONEER_HEADER_SPACE 4225

/** @brief Pioneer bit mark duration in microseconds. */
#define PIONEER_BIT_MARK 500

/** @brief Pioneer one-bit space duration in microseconds. */
#define PIONEER_ONE_SPACE 1500

/** @brief Pioneer zero-bit space duration in microseconds. */
#define PIONEER_ZERO_SPACE 500

/** @brief Number of data bits in a Pioneer frame (NEC-style addr/~addr/cmd/~cmd). */
#define PIONEER_FRAME_BITS 32

/** @brief Minimum number of RMT symbols for a valid Pioneer frame. */
#define PIONEER_MIN_SYMBOLS 34

/** @brief Bit position of the inverted address field in the Pioneer frame word. */
#define PIONEER_ADDR_INV_SHIFT 8

/** @brief Bit position of the command field in the Pioneer frame word. */
#define PIONEER_CMD_SHIFT 16

/** @brief Bit position of the inverted command field in the Pioneer frame word. */
#define PIONEER_CMD_INV_SHIFT 24

/** @brief XOR result expected when a byte and its complement are combined. */
#define PIONEER_INTEGRITY_MASK 0xFF

/** @brief Maximum address value for a standard Pioneer frame (8-bit device). */
#define PIONEER_ADDR_STANDARD_MAX 0xFF

/** @brief Bit mask for the 16-bit extended address field in a Pioneer frame. */
#define PIONEER_EXT_ADDR_MASK 0xFFFF

/**
 * @brief Decode a Pioneer IR frame from RMT symbols.
 *
 * Pioneer shares NEC-style framing but uses a distinct preamble (8500/4225 vs
 * NEC 9000/4500). It is decoded before NEC using a strict preamble tolerance so
 * genuine Pioneer frames are separated from NEC. Frames whose timing falls in
 * the narrow overlap band between the two remain inherently ambiguous.
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a valid Pioneer frame was decoded, false otherwise.
 */
bool ir_protocol_pioneer_decode(const rmt_symbol_word_t *symbols,
                                size_t count,
                                ir_data_t *out_data);

/**
 * @brief Encode a Pioneer IR command into RMT symbols.
 *
 * Uses the Pioneer 40 kHz timing and appends inverted address and command
 * bytes for integrity.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_protocol_pioneer_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_PIONEER_H

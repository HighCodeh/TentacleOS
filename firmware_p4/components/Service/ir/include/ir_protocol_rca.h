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

#ifndef IR_PROTOCOL_RCA_H
#define IR_PROTOCOL_RCA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_protocol.h"

/** @brief RCA preamble mark duration in microseconds. */
#define RCA_HEADER_MARK 4000

/** @brief RCA preamble space duration in microseconds. */
#define RCA_HEADER_SPACE 4000

/** @brief RCA bit mark duration in microseconds. */
#define RCA_BIT_MARK 500

/** @brief RCA one-bit space duration in microseconds. */
#define RCA_ONE_SPACE 2000

/** @brief RCA zero-bit space duration in microseconds. */
#define RCA_ZERO_SPACE 1000

/** @brief Number of data bits in an RCA frame (4 addr + 8 cmd + 4 ~addr + 8 ~cmd). */
#define RCA_FRAME_BITS 24

/** @brief Minimum number of RMT symbols for a valid RCA frame (header + 24 bits). */
#define RCA_MIN_SYMBOLS 25

/** @brief Bit position of the 4-bit address field in the MSB-first RCA frame word. */
#define RCA_ADDR_SHIFT 20

/** @brief Bit position of the 8-bit command field in the MSB-first RCA frame word. */
#define RCA_CMD_SHIFT 12

/** @brief Bit position of the 4-bit inverted address field in the RCA frame word. */
#define RCA_ADDR_INV_SHIFT 8

/** @brief Bit mask for the 4-bit address field. */
#define RCA_ADDR_MASK 0x0F

/** @brief Bit mask for the 8-bit command field. */
#define RCA_CMD_MASK 0xFF

/** @brief XOR result expected when the 4-bit address and its complement are combined. */
#define RCA_ADDR_INTEGRITY_MASK 0x0F

/** @brief XOR result expected when the 8-bit command and its complement are combined. */
#define RCA_CMD_INTEGRITY_MASK 0xFF

/**
 * @brief Decode an RCA IR frame from RMT symbols.
 *
 * Validates the preamble and the inverted address/command integrity fields.
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a valid RCA frame was decoded, false otherwise.
 */
bool ir_protocol_rca_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data);

/**
 * @brief Encode an RCA IR command into RMT symbols.
 *
 * Packs the 4-bit address and 8-bit command MSB-first and appends their
 * complements for integrity.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_protocol_rca_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_RCA_H

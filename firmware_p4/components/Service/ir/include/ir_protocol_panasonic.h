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

#ifndef IR_PROTOCOL_PANASONIC_H
#define IR_PROTOCOL_PANASONIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir_protocol.h"

/** @brief Panasonic header mark duration in microseconds. */
#define PANASONIC_HEADER_MARK 3456

/** @brief Panasonic header space duration in microseconds. */
#define PANASONIC_HEADER_SPACE 1728

/** @brief Panasonic bit mark duration in microseconds. */
#define PANASONIC_BIT_MARK 432

/** @brief Panasonic one-bit space duration in microseconds. */
#define PANASONIC_ONE_SPACE 1296

/** @brief Panasonic zero-bit space duration in microseconds. */
#define PANASONIC_ZERO_SPACE 432

/** @brief Panasonic manufacturer vendor ID embedded in every frame. */
#define PANASONIC_VENDOR_ID 0x2002

/** @brief Number of data bits in a Panasonic frame. */
#define PANASONIC_FRAME_BITS 48

/** @brief Minimum number of RMT symbols for a valid Panasonic frame. */
#define PANASONIC_MIN_SYMBOLS 50

/** @brief Bit shift between consecutive nibbles (bits per nibble). */
#define PANASONIC_NIBBLE_SHIFT 4

/** @brief Mask to extract a single nibble. */
#define PANASONIC_NIBBLE_MASK 0x0F

/** @brief Bit shift to access the high byte of a 16-bit word. */
#define PANASONIC_BYTE_SHIFT 8

/** @brief Bit position of byte0 in the Panasonic frame word. */
#define PANASONIC_BYTE0_SHIFT 16

/** @brief Bit position of byte1 in the Panasonic frame word. */
#define PANASONIC_BYTE1_SHIFT 24

/** @brief Bit position of byte2 in the Panasonic frame word. */
#define PANASONIC_BYTE2_SHIFT 32

/** @brief Bit position of byte3 in the Panasonic frame word. */
#define PANASONIC_BYTE3_SHIFT 40

/** @brief Bit mask for the 16-bit vendor ID field in the Panasonic frame word. */
#define PANASONIC_VENDOR_MASK 0xFFFF

/** @brief Bit mask for the 10-bit Kaseikyo data (command) field. */
#define KASEIKYO_DATA_MASK 0x3FF

/** @brief Bit mask for the high 6 bits of the Kaseikyo data field carried in frame byte 4. */
#define KASEIKYO_DATA_HI_MASK 0x3F

/** @brief Bit mask for the 2-bit Kaseikyo id field. */
#define KASEIKYO_ID_MASK 0x03

/** @brief Bit position of the id field within frame byte 4. */
#define KASEIKYO_ID_SHIFT 6

/** @brief Bit position of the id field within the packed address. */
#define KASEIKYO_ID_ADDR_SHIFT 24

/**
 * @brief Decode a Panasonic IR frame from RMT symbols.
 *
 * Validates header, vendor ID, and checksum byte before accepting the frame.
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a valid Panasonic frame was decoded, false otherwise.
 */
bool ir_protocol_panasonic_decode(const rmt_symbol_word_t *symbols,
                                  size_t count,
                                  ir_data_t *out_data);

/**
 * @brief Encode a Panasonic IR command into RMT symbols.
 *
 * Embeds the vendor ID and appends a checksum byte computed from the address
 * and command fields.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_protocol_panasonic_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_PANASONIC_H
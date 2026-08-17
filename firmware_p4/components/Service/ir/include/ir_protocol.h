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

#ifndef IR_PROTOCOL_H
#define IR_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "driver/rmt_types.h"

/** @brief Tolerance window for pulse matching, in percent. */
#define IR_TOLERANCE 25

/**
 * @brief Tight tolerance, in percent, for disambiguating protocols whose
 * preambles are close together (e.g. Pioneer 8500/4225 vs NEC 9000/4500).
 */
#define IR_TOLERANCE_STRICT 6

/** @brief Default carrier frequency in Hz (NEC, Samsung, LG, JVC, Denon). */
#define IR_CARRIER_HZ_DEFAULT 38000

/** @brief Carrier frequency in Hz for RC5 and RC6. */
#define IR_CARRIER_HZ_RC5_RC6 36000

/** @brief Carrier frequency in Hz for Sony. */
#define IR_CARRIER_HZ_SONY 40000

/** @brief Carrier frequency in Hz for Panasonic. */
#define IR_CARRIER_HZ_PANASONIC 37000

/** @brief Carrier frequency in Hz for Pioneer. */
#define IR_CARRIER_HZ_PIONEER 40000

/**
 * @brief Supported IR protocols.
 */
typedef enum {
  IR_PROTO_UNKNOWN = 0,
  IR_PROTO_NEC,
  IR_PROTO_SAMSUNG,
  IR_PROTO_RC6,
  IR_PROTO_RC5,
  IR_PROTO_SONY,
  IR_PROTO_LG,
  IR_PROTO_JVC,
  IR_PROTO_DENON,
  IR_PROTO_PANASONIC,
  IR_PROTO_RCA,
  IR_PROTO_PIONEER,
  IR_PROTO_NEC42,
  IR_PROTO_COUNT,
} ir_protocol_t;

/**
 * @brief Decoded IR frame data.
 */
typedef struct {
  ir_protocol_t protocol;
  uint32_t address;
  uint32_t command;
  bool repeat;
} ir_data_t;

/**
 * @brief Configuration for pulse-distance decoding.
 */
typedef struct {
  uint32_t one_space;
  uint32_t zero_space;
  bool msb_first;
} ir_pulse_distance_cfg_t;

/**
 * @brief Configuration for pulse-width decoding.
 */
typedef struct {
  uint32_t one_mark;
  uint32_t zero_mark;
  bool msb_first;
} ir_pulse_width_cfg_t;

/**
 * @brief Configuration for pulse-distance encoding.
 */
typedef struct {
  uint32_t header_mark;
  uint32_t header_space;
  uint32_t bit_mark;
  uint32_t one_space;
  uint32_t zero_space;
  size_t max;
  bool msb_first;
  bool stop_bit;
} ir_encode_distance_cfg_t;

/**
 * @brief Configuration for pulse-width encoding.
 */
typedef struct {
  uint32_t header_mark;
  uint32_t header_space;
  uint32_t one_mark;
  uint32_t zero_mark;
  uint32_t bit_space;
  size_t max;
  bool msb_first;
  bool stop_bit;
} ir_encode_width_cfg_t;

/**
 * @brief Check if a measured pulse duration matches an expected value within tolerance.
 *
 * @param[in] measured_us  Measured duration in microseconds.
 * @param[in] expected_us  Expected duration in microseconds.
 *
 * @return true if within IR_TOLERANCE percent of expected, false otherwise.
 */
bool ir_match(uint32_t measured_us, uint32_t expected_us);

/**
 * @brief Check if a measured pulse duration matches an expected value within a
 *        caller-supplied tolerance.
 *
 * @param[in] measured_us   Measured duration in microseconds.
 * @param[in] expected_us   Expected duration in microseconds.
 * @param[in] tol_percent   Tolerance window, in percent.
 *
 * @return true if within @p tol_percent of expected, false otherwise.
 */
bool ir_match_tol(uint32_t measured_us, uint32_t expected_us, uint32_t tol_percent);

/**
 * @brief Get the display name of a protocol.
 *
 * @param[in] proto  Protocol identifier.
 *
 * @return Null-terminated string name, or "UNKNOWN" for unrecognized values.
 */
const char *ir_protocol_name(ir_protocol_t proto);

/**
 * @brief Get the carrier frequency for a protocol.
 *
 * @param[in] proto  Protocol identifier.
 *
 * @return Carrier frequency in Hz.
 */
uint32_t ir_carrier_freq(ir_protocol_t proto);

/**
 * @brief Try to decode an IR frame using all known protocols.
 *
 * @param[in]  symbols   RMT symbol buffer. Must not be NULL.
 * @param[in]  count     Number of symbols. Must be greater than 0.
 * @param[out] out_data  Destination for decoded data. Must not be NULL.
 *
 * @return true if a protocol matched, false otherwise.
 */
bool ir_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data);

/**
 * @brief Encode an IR command into RMT symbols.
 *
 * @param[in]  data     IR command to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Decode bits from a pulse-distance modulated symbol stream.
 *
 * @param[in] symbols   RMT symbol buffer. Must not be NULL.
 * @param[in] offset    Index of the first symbol to decode.
 * @param[in] num_bits  Number of bits to decode. Must be greater than 0.
 * @param[in] cfg       Decode configuration. Must not be NULL.
 *
 * @return Decoded bit pattern, or 0 on invalid arguments.
 */
uint64_t ir_decode_pulse_distance(const rmt_symbol_word_t *symbols,
                                  size_t offset,
                                  size_t num_bits,
                                  const ir_pulse_distance_cfg_t *cfg);

/**
 * @brief Decode bits from a pulse-width modulated symbol stream.
 *
 * @param[in] symbols   RMT symbol buffer. Must not be NULL.
 * @param[in] offset    Index of the first symbol to decode.
 * @param[in] num_bits  Number of bits to decode. Must be greater than 0.
 * @param[in] cfg       Decode configuration. Must not be NULL.
 *
 * @return Decoded bit pattern, or 0 on invalid arguments.
 */
uint64_t ir_decode_pulse_width(const rmt_symbol_word_t *symbols,
                               size_t offset,
                               size_t num_bits,
                               const ir_pulse_width_cfg_t *cfg);

/**
 * @brief Encode bits into a pulse-distance modulated symbol stream.
 *
 * @param[out] symbols   Destination buffer. Must not be NULL.
 * @param[in]  data      Bit pattern to encode.
 * @param[in]  num_bits  Number of bits to encode. Must be greater than 0.
 * @param[in]  cfg       Encode configuration. Must not be NULL. cfg->max is the buffer capacity.
 *
 * @return Number of symbols written, or 0 on failure or buffer overflow.
 */
size_t ir_encode_pulse_distance(rmt_symbol_word_t *symbols,
                                uint64_t data,
                                size_t num_bits,
                                const ir_encode_distance_cfg_t *cfg);

/**
 * @brief Encode bits into a pulse-width modulated symbol stream.
 *
 * @param[out] symbols   Destination buffer. Must not be NULL.
 * @param[in]  data      Bit pattern to encode.
 * @param[in]  num_bits  Number of bits to encode. Must be greater than 0.
 * @param[in]  cfg       Encode configuration. Must not be NULL. cfg->max is the buffer capacity.
 *
 * @return Number of symbols written, or 0 on failure or buffer overflow.
 */
size_t ir_encode_pulse_width(rmt_symbol_word_t *symbols,
                             uint64_t data,
                             size_t num_bits,
                             const ir_encode_width_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_H
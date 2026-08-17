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

#ifndef IR_AC_H
#define IR_AC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/rmt_types.h"

/**
 * @brief Supported air-conditioner protocols.
 *
 * Unlike remote-control protocols, each AC frame carries the full appliance
 * state (power, mode, temperature, fan) rather than a single command.
 */
typedef enum {
  IR_AC_PROTO_UNKNOWN = 0,
  IR_AC_PROTO_COOLIX,
  IR_AC_PROTO_GREE,
  IR_AC_PROTO_LG,
  IR_AC_PROTO_MIDEA,
  IR_AC_PROTO_TOSHIBA,
  IR_AC_PROTO_HAIER,
  IR_AC_PROTO_COUNT,
} ir_ac_protocol_t;

/** @brief Operating mode. */
typedef enum {
  IR_AC_MODE_AUTO = 0,
  IR_AC_MODE_COOL,
  IR_AC_MODE_DRY,
  IR_AC_MODE_HEAT,
  IR_AC_MODE_FAN,
} ir_ac_mode_t;

/** @brief Fan speed. */
typedef enum {
  IR_AC_FAN_AUTO = 0,
  IR_AC_FAN_LOW,
  IR_AC_FAN_MED,
  IR_AC_FAN_HIGH,
} ir_ac_fan_t;

/**
 * @brief Full air-conditioner state encoded into a single IR frame.
 *
 * temp_c is clamped to the target protocol's supported range during encoding.
 */
typedef struct {
  ir_ac_protocol_t protocol;
  bool power;
  ir_ac_mode_t mode;
  uint8_t temp_c;
  ir_ac_fan_t fan;
} ir_ac_state_t;

/**
 * @brief Get the display name of an AC protocol.
 *
 * @param[in] proto  Protocol identifier.
 *
 * @return Null-terminated string name, or "UNKNOWN" for unrecognized values.
 */
const char *ir_ac_protocol_name(ir_ac_protocol_t proto);

/**
 * @brief Get the display name of an AC operating mode.
 *
 * @param[in] mode  Mode identifier.
 *
 * @return Null-terminated string name.
 */
const char *ir_ac_mode_name(ir_ac_mode_t mode);

/**
 * @brief Get the display name of an AC fan speed.
 *
 * @param[in] fan  Fan speed identifier.
 *
 * @return Null-terminated string name.
 */
const char *ir_ac_fan_name(ir_ac_fan_t fan);

/**
 * @brief Get the carrier frequency for an AC protocol.
 *
 * @param[in] proto  Protocol identifier.
 *
 * @return Carrier frequency in Hz.
 */
uint32_t ir_ac_carrier_freq(ir_ac_protocol_t proto);

/**
 * @brief Encode an AC state into RMT symbols.
 *
 * @param[in]  state    AC state to encode. Must not be NULL.
 * @param[out] symbols  Destination buffer. Must not be NULL.
 * @param[in]  max      Capacity of @p symbols in symbols.
 *
 * @return Number of symbols written, or 0 on failure.
 */
size_t ir_ac_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max);

/**
 * @brief Encode and transmit an AC state over IR.
 *
 * Requires ir_tx_init() to have been called.
 *
 * @param[in] state  AC state to transmit. Must not be NULL.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if encoding produces no symbols
 *   - Other ESP_ERR codes from the IR driver
 */
esp_err_t ir_ac_send(const ir_ac_state_t *state);

/**
 * @brief Try to decode RMT symbols into an AC state using all known protocols.
 *
 * @param[in]  symbols    RMT symbol buffer. Must not be NULL.
 * @param[in]  count      Number of symbols. Must be greater than 0.
 * @param[out] out_state  Destination for the decoded state. Must not be NULL.
 *
 * @return true if an AC protocol matched, false otherwise.
 */
bool ir_ac_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // IR_AC_H

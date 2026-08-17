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

#include "ir_ac_lg.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_AC_LG";

#define LGAC_SIGNATURE   0x88
#define LGAC_SIGN_SHIFT  20
#define LGAC_POWER_SHIFT 18
#define LGAC_MODE_SHIFT  12
#define LGAC_TEMP_SHIFT  8
#define LGAC_FAN_SHIFT   4

#define LGAC_POWER_ON  0
#define LGAC_POWER_OFF 3

#define LGAC_MODE_COOL 0
#define LGAC_MODE_DRY  1
#define LGAC_MODE_FAN  2
#define LGAC_MODE_AUTO 3
#define LGAC_MODE_HEAT 4

#define LGAC_FAN_LOW  1
#define LGAC_FAN_MED  2
#define LGAC_FAN_HIGH 4
#define LGAC_FAN_AUTO 5

#define LGAC_TEMP_MIN    16
#define LGAC_TEMP_MAX    30
#define LGAC_TEMP_ADJUST 15

#define LGAC_OFF_COMMAND 0x88C0051u
#define LGAC_MIN_SYMBOLS 29

static uint8_t lgac_checksum(uint32_t raw);

size_t ir_ac_lg_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max) {
  if (state == NULL || symbols == NULL || max == 0) {
    ESP_LOGE(TAG, "encode: invalid arguments");
    return 0;
  }

  uint32_t raw;
  if (!state->power) {
    raw = LGAC_OFF_COMMAND;
  } else {
    uint8_t mode_code;
    switch (state->mode) {
      case IR_AC_MODE_COOL:
        mode_code = LGAC_MODE_COOL;
        break;
      case IR_AC_MODE_DRY:
        mode_code = LGAC_MODE_DRY;
        break;
      case IR_AC_MODE_FAN:
        mode_code = LGAC_MODE_FAN;
        break;
      case IR_AC_MODE_HEAT:
        mode_code = LGAC_MODE_HEAT;
        break;
      case IR_AC_MODE_AUTO:
      default:
        mode_code = LGAC_MODE_AUTO;
        break;
    }

    uint8_t fan_code;
    switch (state->fan) {
      case IR_AC_FAN_LOW:
        fan_code = LGAC_FAN_LOW;
        break;
      case IR_AC_FAN_MED:
        fan_code = LGAC_FAN_MED;
        break;
      case IR_AC_FAN_HIGH:
        fan_code = LGAC_FAN_HIGH;
        break;
      case IR_AC_FAN_AUTO:
      default:
        fan_code = LGAC_FAN_AUTO;
        break;
    }

    uint8_t temp = state->temp_c;
    if (temp < LGAC_TEMP_MIN)
      temp = LGAC_TEMP_MIN;
    if (temp > LGAC_TEMP_MAX)
      temp = LGAC_TEMP_MAX;

    raw = ((uint32_t)LGAC_SIGNATURE << LGAC_SIGN_SHIFT) |
          ((uint32_t)LGAC_POWER_ON << LGAC_POWER_SHIFT) | ((uint32_t)mode_code << LGAC_MODE_SHIFT) |
          ((uint32_t)(temp - LGAC_TEMP_ADJUST) << LGAC_TEMP_SHIFT) |
          ((uint32_t)fan_code << LGAC_FAN_SHIFT);
    raw |= lgac_checksum(raw);
  }

  ir_encode_distance_cfg_t cfg = {
      .header_mark = LGAC_HDR_MARK,
      .header_space = LGAC_HDR_SPACE,
      .bit_mark = LGAC_BIT_MARK,
      .one_space = LGAC_ONE_SPACE,
      .zero_space = LGAC_ZERO_SPACE,
      .max = max,
      .msb_first = true,
      .stop_bit = true,
  };
  return ir_encode_pulse_distance(symbols, raw, LGAC_FRAME_BITS, &cfg);
}

bool ir_ac_lg_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state) {
  if (symbols == NULL || count == 0 || out_state == NULL) {
    ESP_LOGE(TAG, "decode: invalid arguments");
    return false;
  }
  if (count < LGAC_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, LGAC_HDR_MARK) ||
      !ir_match(symbols[0].duration1, LGAC_HDR_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = LGAC_ONE_SPACE,
      .zero_space = LGAC_ZERO_SPACE,
      .msb_first = true,
  };
  uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 1, LGAC_FRAME_BITS, &cfg);

  if (((raw >> LGAC_SIGN_SHIFT) & 0xFF) != LGAC_SIGNATURE)
    return false;
  if ((raw & 0xF) != lgac_checksum(raw))
    return false;

  out_state->protocol = IR_AC_PROTO_LG;

  if (((raw >> LGAC_POWER_SHIFT) & 0x3) == LGAC_POWER_OFF) {
    out_state->power = false;
    out_state->mode = IR_AC_MODE_COOL;
    out_state->temp_c = LGAC_TEMP_MIN;
    out_state->fan = IR_AC_FAN_AUTO;
    return true;
  }
  out_state->power = true;

  switch ((raw >> LGAC_MODE_SHIFT) & 0x7) {
    case LGAC_MODE_COOL:
      out_state->mode = IR_AC_MODE_COOL;
      break;
    case LGAC_MODE_DRY:
      out_state->mode = IR_AC_MODE_DRY;
      break;
    case LGAC_MODE_FAN:
      out_state->mode = IR_AC_MODE_FAN;
      break;
    case LGAC_MODE_HEAT:
      out_state->mode = IR_AC_MODE_HEAT;
      break;
    case LGAC_MODE_AUTO:
    default:
      out_state->mode = IR_AC_MODE_AUTO;
      break;
  }

  out_state->temp_c = (uint8_t)(((raw >> LGAC_TEMP_SHIFT) & 0xF) + LGAC_TEMP_ADJUST);

  switch ((raw >> LGAC_FAN_SHIFT) & 0xF) {
    case LGAC_FAN_LOW:
      out_state->fan = IR_AC_FAN_LOW;
      break;
    case LGAC_FAN_MED:
      out_state->fan = IR_AC_FAN_MED;
      break;
    case LGAC_FAN_HIGH:
      out_state->fan = IR_AC_FAN_HIGH;
      break;
    case LGAC_FAN_AUTO:
    default:
      out_state->fan = IR_AC_FAN_AUTO;
      break;
  }
  return true;
}

static uint8_t lgac_checksum(uint32_t raw) {
  return (uint8_t)((((raw >> 4) & 0xF) + ((raw >> 8) & 0xF) + ((raw >> 12) & 0xF) +
                    ((raw >> 16) & 0xF)) &
                   0xF);
}

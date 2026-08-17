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

#include "ir_ac_toshiba.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_AC_TOSHIBA";

#define TOSHIBA_MODE_AUTO 0
#define TOSHIBA_MODE_COOL 1
#define TOSHIBA_MODE_DRY  2
#define TOSHIBA_MODE_HEAT 3
#define TOSHIBA_MODE_FAN  4

#define TOSHIBA_FAN_AUTO 0
#define TOSHIBA_FAN_MIN  1
#define TOSHIBA_FAN_MED  3
#define TOSHIBA_FAN_MAX  5

#define TOSHIBA_TEMP_MIN    17
#define TOSHIBA_TEMP_MAX    30
#define TOSHIBA_TEMP_ADJUST 17

#define TOSHIBA_TEMP_SHIFT 4
#define TOSHIBA_FAN_SHIFT  5
#define TOSHIBA_MODE_MASK  0x7

#define TOSHIBA_HEADER0 0xF2
#define TOSHIBA_HEADER1 0x0D

#define TOSHIBA_MIN_SYMBOLS (1 + TOSHIBA_FRAME_BITS)

static uint8_t toshiba_checksum(const uint8_t *bytes);

size_t ir_ac_toshiba_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max) {
  if (state == NULL || symbols == NULL || max == 0) {
    ESP_LOGE(TAG, "encode: invalid arguments");
    return 0;
  }

  uint8_t mode_code;
  switch (state->mode) {
    case IR_AC_MODE_COOL:
      mode_code = TOSHIBA_MODE_COOL;
      break;
    case IR_AC_MODE_DRY:
      mode_code = TOSHIBA_MODE_DRY;
      break;
    case IR_AC_MODE_HEAT:
      mode_code = TOSHIBA_MODE_HEAT;
      break;
    case IR_AC_MODE_FAN:
      mode_code = TOSHIBA_MODE_FAN;
      break;
    case IR_AC_MODE_AUTO:
    default:
      mode_code = TOSHIBA_MODE_AUTO;
      break;
  }

  uint8_t fan_code;
  switch (state->fan) {
    case IR_AC_FAN_LOW:
      fan_code = TOSHIBA_FAN_MIN;
      break;
    case IR_AC_FAN_MED:
      fan_code = TOSHIBA_FAN_MED;
      break;
    case IR_AC_FAN_HIGH:
      fan_code = TOSHIBA_FAN_MAX;
      break;
    case IR_AC_FAN_AUTO:
    default:
      fan_code = TOSHIBA_FAN_AUTO;
      break;
  }

  uint8_t temp = state->temp_c;
  if (temp < TOSHIBA_TEMP_MIN)
    temp = TOSHIBA_TEMP_MIN;
  if (temp > TOSHIBA_TEMP_MAX)
    temp = TOSHIBA_TEMP_MAX;

  uint8_t bytes[TOSHIBA_STATE_LEN] = {
      TOSHIBA_HEADER0, TOSHIBA_HEADER1, 0x03, 0xFC, 0x01, 0x00, 0x00, 0x00, 0x00};
  bytes[5] = (uint8_t)((temp - TOSHIBA_TEMP_ADJUST) << TOSHIBA_TEMP_SHIFT);
  bytes[6] = (uint8_t)((mode_code & TOSHIBA_MODE_MASK) | (fan_code << TOSHIBA_FAN_SHIFT));
  bytes[TOSHIBA_STATE_LEN - 1] = toshiba_checksum(bytes);

  ir_encode_distance_cfg_t cfg = {
      .header_mark = TOSHIBA_HDR_MARK,
      .header_space = TOSHIBA_HDR_SPACE,
      .bit_mark = TOSHIBA_BIT_MARK,
      .one_space = TOSHIBA_ONE_SPACE,
      .zero_space = TOSHIBA_ZERO_SPACE,
      .max = max,
      .msb_first = true,
      .stop_bit = false,
  };

  size_t idx = 0;
  size_t n = ir_encode_pulse_distance(symbols, bytes[0], 8, &cfg);
  if (n == 0)
    return 0;
  idx += n;

  cfg.header_mark = 0;
  cfg.header_space = 0;
  for (size_t i = 1; i < TOSHIBA_STATE_LEN; i++) {
    cfg.max = max - idx;
    n = ir_encode_pulse_distance(symbols + idx, bytes[i], 8, &cfg);
    if (n == 0)
      return 0;
    idx += n;
  }

  if (idx + 1 > max)
    return 0;
  symbols[idx].duration0 = TOSHIBA_BIT_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = TOSHIBA_MIN_GAP;
  symbols[idx].level1 = 0;
  idx++;

  return idx;
}

bool ir_ac_toshiba_decode(const rmt_symbol_word_t *symbols,
                          size_t count,
                          ir_ac_state_t *out_state) {
  if (symbols == NULL || count == 0 || out_state == NULL) {
    ESP_LOGE(TAG, "decode: invalid arguments");
    return false;
  }
  if (count < TOSHIBA_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, TOSHIBA_HDR_MARK) ||
      !ir_match(symbols[0].duration1, TOSHIBA_HDR_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = TOSHIBA_ONE_SPACE,
      .zero_space = TOSHIBA_ZERO_SPACE,
      .msb_first = true,
  };

  uint8_t bytes[TOSHIBA_STATE_LEN];
  for (size_t i = 0; i < TOSHIBA_STATE_LEN; i++)
    bytes[i] = (uint8_t)ir_decode_pulse_distance(symbols, 1 + i * 8, 8, &cfg);

  if (bytes[0] != TOSHIBA_HEADER0 || bytes[1] != TOSHIBA_HEADER1)
    return false;
  if (bytes[TOSHIBA_STATE_LEN - 1] != toshiba_checksum(bytes))
    return false;

  out_state->protocol = IR_AC_PROTO_TOSHIBA;
  out_state->power = true;

  switch (bytes[6] & TOSHIBA_MODE_MASK) {
    case TOSHIBA_MODE_COOL:
      out_state->mode = IR_AC_MODE_COOL;
      break;
    case TOSHIBA_MODE_DRY:
      out_state->mode = IR_AC_MODE_DRY;
      break;
    case TOSHIBA_MODE_HEAT:
      out_state->mode = IR_AC_MODE_HEAT;
      break;
    case TOSHIBA_MODE_FAN:
      out_state->mode = IR_AC_MODE_FAN;
      break;
    case TOSHIBA_MODE_AUTO:
    default:
      out_state->mode = IR_AC_MODE_AUTO;
      break;
  }

  out_state->temp_c = (uint8_t)((bytes[5] >> TOSHIBA_TEMP_SHIFT) + TOSHIBA_TEMP_ADJUST);

  switch (bytes[6] >> TOSHIBA_FAN_SHIFT) {
    case TOSHIBA_FAN_MIN:
      out_state->fan = IR_AC_FAN_LOW;
      break;
    case TOSHIBA_FAN_MED:
      out_state->fan = IR_AC_FAN_MED;
      break;
    case TOSHIBA_FAN_MAX:
      out_state->fan = IR_AC_FAN_HIGH;
      break;
    case TOSHIBA_FAN_AUTO:
    default:
      out_state->fan = IR_AC_FAN_AUTO;
      break;
  }
  return true;
}

static uint8_t toshiba_checksum(const uint8_t *bytes) {
  uint8_t sum = 0;
  for (size_t i = 0; i < TOSHIBA_STATE_LEN - 1; i++)
    sum ^= bytes[i];
  return sum;
}

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

#include "ir_ac_haier.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_AC_HAIER";

#define HAIER_PREFIX  0xA5
#define HAIER_CMD_ON  0x1
#define HAIER_CMD_OFF 0x0

#define HAIER_MODE_AUTO 0
#define HAIER_MODE_COOL 1
#define HAIER_MODE_DRY  2
#define HAIER_MODE_HEAT 3
#define HAIER_MODE_FAN  4

#define HAIER_FAN_AUTO 0
#define HAIER_FAN_LOW  1
#define HAIER_FAN_MED  2
#define HAIER_FAN_HIGH 3

#define HAIER_TEMP_MIN    16
#define HAIER_TEMP_MAX    30
#define HAIER_TEMP_ADJUST 16

#define HAIER_TEMP_SHIFT 4
#define HAIER_FAN_SHIFT  6
#define HAIER_MODE_SHIFT 5

#define HAIER_BYTE2_UNKNOWN 0x20
#define HAIER_BYTE4_DEFAULT 0x0C

#define HAIER_MIN_SYMBOLS (2 + HAIER_FRAME_BITS)

static uint8_t haier_checksum(const uint8_t *bytes);

size_t ir_ac_haier_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max) {
  if (state == NULL || symbols == NULL || max == 0) {
    ESP_LOGE(TAG, "encode: invalid arguments");
    return 0;
  }

  uint8_t mode_code;
  switch (state->mode) {
    case IR_AC_MODE_COOL:
      mode_code = HAIER_MODE_COOL;
      break;
    case IR_AC_MODE_DRY:
      mode_code = HAIER_MODE_DRY;
      break;
    case IR_AC_MODE_HEAT:
      mode_code = HAIER_MODE_HEAT;
      break;
    case IR_AC_MODE_FAN:
      mode_code = HAIER_MODE_FAN;
      break;
    case IR_AC_MODE_AUTO:
    default:
      mode_code = HAIER_MODE_AUTO;
      break;
  }

  uint8_t fan_code;
  switch (state->fan) {
    case IR_AC_FAN_LOW:
      fan_code = HAIER_FAN_LOW;
      break;
    case IR_AC_FAN_MED:
      fan_code = HAIER_FAN_MED;
      break;
    case IR_AC_FAN_HIGH:
      fan_code = HAIER_FAN_HIGH;
      break;
    case IR_AC_FAN_AUTO:
    default:
      fan_code = HAIER_FAN_AUTO;
      break;
  }

  uint8_t temp = state->temp_c;
  if (temp < HAIER_TEMP_MIN)
    temp = HAIER_TEMP_MIN;
  if (temp > HAIER_TEMP_MAX)
    temp = HAIER_TEMP_MAX;

  uint8_t bytes[HAIER_STATE_LEN] = {
      HAIER_PREFIX, 0x00, HAIER_BYTE2_UNKNOWN, 0x00, HAIER_BYTE4_DEFAULT, 0x00, 0x00, 0x00, 0x00};
  uint8_t command = state->power ? HAIER_CMD_ON : HAIER_CMD_OFF;
  bytes[1] = (uint8_t)(command | ((temp - HAIER_TEMP_ADJUST) << HAIER_TEMP_SHIFT));
  bytes[5] = (uint8_t)(fan_code << HAIER_FAN_SHIFT);
  bytes[6] = (uint8_t)(mode_code << HAIER_MODE_SHIFT);
  bytes[HAIER_STATE_LEN - 1] = haier_checksum(bytes);

  size_t idx = 0;
  if (idx + 1 > max)
    return 0;
  symbols[idx].duration0 = HAIER_PRE_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = HAIER_PRE_SPACE;
  symbols[idx].level1 = 0;
  idx++;

  ir_encode_distance_cfg_t cfg = {
      .header_mark = HAIER_HDR_MARK,
      .header_space = HAIER_HDR_GAP,
      .bit_mark = HAIER_BIT_MARK,
      .one_space = HAIER_ONE_SPACE,
      .zero_space = HAIER_ZERO_SPACE,
      .max = max - idx,
      .msb_first = true,
      .stop_bit = false,
  };
  size_t n = ir_encode_pulse_distance(symbols + idx, bytes[0], 8, &cfg);
  if (n == 0)
    return 0;
  idx += n;

  cfg.header_mark = 0;
  cfg.header_space = 0;
  for (size_t i = 1; i < HAIER_STATE_LEN; i++) {
    cfg.max = max - idx;
    n = ir_encode_pulse_distance(symbols + idx, bytes[i], 8, &cfg);
    if (n == 0)
      return 0;
    idx += n;
  }

  if (idx + 1 > max)
    return 0;
  symbols[idx].duration0 = HAIER_BIT_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = 0;
  symbols[idx].level1 = 0;
  idx++;

  return idx;
}

bool ir_ac_haier_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state) {
  if (symbols == NULL || count == 0 || out_state == NULL) {
    ESP_LOGE(TAG, "decode: invalid arguments");
    return false;
  }
  if (count < HAIER_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, HAIER_PRE_MARK) ||
      !ir_match(symbols[0].duration1, HAIER_PRE_SPACE))
    return false;
  if (!ir_match(symbols[1].duration0, HAIER_HDR_MARK) ||
      !ir_match(symbols[1].duration1, HAIER_HDR_GAP))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = HAIER_ONE_SPACE,
      .zero_space = HAIER_ZERO_SPACE,
      .msb_first = true,
  };

  uint8_t bytes[HAIER_STATE_LEN];
  for (size_t i = 0; i < HAIER_STATE_LEN; i++)
    bytes[i] = (uint8_t)ir_decode_pulse_distance(symbols, 2 + i * 8, 8, &cfg);

  if (bytes[0] != HAIER_PREFIX)
    return false;
  if (bytes[HAIER_STATE_LEN - 1] != haier_checksum(bytes))
    return false;

  out_state->protocol = IR_AC_PROTO_HAIER;
  out_state->power = (bytes[1] & 0x0F) != HAIER_CMD_OFF;

  switch (bytes[6] >> HAIER_MODE_SHIFT) {
    case HAIER_MODE_COOL:
      out_state->mode = IR_AC_MODE_COOL;
      break;
    case HAIER_MODE_DRY:
      out_state->mode = IR_AC_MODE_DRY;
      break;
    case HAIER_MODE_HEAT:
      out_state->mode = IR_AC_MODE_HEAT;
      break;
    case HAIER_MODE_FAN:
      out_state->mode = IR_AC_MODE_FAN;
      break;
    case HAIER_MODE_AUTO:
    default:
      out_state->mode = IR_AC_MODE_AUTO;
      break;
  }

  out_state->temp_c = (uint8_t)((bytes[1] >> HAIER_TEMP_SHIFT) + HAIER_TEMP_ADJUST);

  switch (bytes[5] >> HAIER_FAN_SHIFT) {
    case HAIER_FAN_LOW:
      out_state->fan = IR_AC_FAN_LOW;
      break;
    case HAIER_FAN_MED:
      out_state->fan = IR_AC_FAN_MED;
      break;
    case HAIER_FAN_HIGH:
      out_state->fan = IR_AC_FAN_HIGH;
      break;
    case HAIER_FAN_AUTO:
    default:
      out_state->fan = IR_AC_FAN_AUTO;
      break;
  }
  return true;
}

static uint8_t haier_checksum(const uint8_t *bytes) {
  uint8_t sum = 0;
  for (size_t i = 0; i < HAIER_STATE_LEN - 1; i++)
    sum += bytes[i];
  return sum;
}

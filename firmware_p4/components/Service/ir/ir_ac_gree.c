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

#include "ir_ac_gree.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_AC_GREE";

#define GREE_MODE_AUTO 0
#define GREE_MODE_COOL 1
#define GREE_MODE_DRY  2
#define GREE_MODE_FAN  3
#define GREE_MODE_HEAT 4

#define GREE_FAN_AUTO 0
#define GREE_FAN_MIN  1
#define GREE_FAN_MED  2
#define GREE_FAN_MAX  3

#define GREE_TEMP_MIN  16
#define GREE_TEMP_MAX  30
#define GREE_AUTO_TEMP 25

#define GREE_CHECKSUM_START 10
#define GREE_BLOCK_BITS     32
#define GREE_MIN_SYMBOLS    69

static uint8_t gree_checksum(const uint8_t *bytes);
static size_t append_footer(rmt_symbol_word_t *symbols, size_t idx, size_t max);

size_t ir_ac_gree_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max) {
  if (state == NULL || symbols == NULL || max == 0) {
    ESP_LOGE(TAG, "encode: invalid arguments");
    return 0;
  }

  uint8_t state_bytes[GREE_STATE_LEN] = {0x00, 0x09, 0x20, 0x50, 0x00, 0x20, 0x00, 0x00};

  uint8_t mode_code;
  switch (state->mode) {
    case IR_AC_MODE_COOL:
      mode_code = GREE_MODE_COOL;
      break;
    case IR_AC_MODE_DRY:
      mode_code = GREE_MODE_DRY;
      break;
    case IR_AC_MODE_HEAT:
      mode_code = GREE_MODE_HEAT;
      break;
    case IR_AC_MODE_FAN:
      mode_code = GREE_MODE_FAN;
      break;
    case IR_AC_MODE_AUTO:
    default:
      mode_code = GREE_MODE_AUTO;
      break;
  }

  uint8_t fan_code;
  switch (state->fan) {
    case IR_AC_FAN_LOW:
      fan_code = GREE_FAN_MIN;
      break;
    case IR_AC_FAN_MED:
      fan_code = GREE_FAN_MED;
      break;
    case IR_AC_FAN_HIGH:
      fan_code = GREE_FAN_MAX;
      break;
    case IR_AC_FAN_AUTO:
    default:
      fan_code = GREE_FAN_AUTO;
      break;
  }
  if (state->mode == IR_AC_MODE_DRY)
    fan_code = GREE_FAN_MIN;

  uint8_t temp = (state->mode == IR_AC_MODE_AUTO) ? GREE_AUTO_TEMP : state->temp_c;
  if (temp < GREE_TEMP_MIN)
    temp = GREE_TEMP_MIN;
  if (temp > GREE_TEMP_MAX)
    temp = GREE_TEMP_MAX;

  state_bytes[0] = (mode_code & 0x7) | ((state->power ? 1u : 0u) << 3) | ((fan_code & 0x3) << 4);
  state_bytes[1] = (state_bytes[1] & 0xF0) | ((uint8_t)(temp - GREE_TEMP_MIN) & 0x0F);

  state_bytes[GREE_STATE_LEN - 1] =
      (state_bytes[GREE_STATE_LEN - 1] & 0x0F) | (uint8_t)(gree_checksum(state_bytes) << 4);

  uint32_t block1 = (uint32_t)state_bytes[0] | ((uint32_t)state_bytes[1] << 8) |
                    ((uint32_t)state_bytes[2] << 16) | ((uint32_t)state_bytes[3] << 24);
  uint32_t block2 = (uint32_t)state_bytes[4] | ((uint32_t)state_bytes[5] << 8) |
                    ((uint32_t)state_bytes[6] << 16) | ((uint32_t)state_bytes[7] << 24);

  ir_encode_distance_cfg_t cfg = {
      .header_mark = GREE_HDR_MARK,
      .header_space = GREE_HDR_SPACE,
      .bit_mark = GREE_BIT_MARK,
      .one_space = GREE_ONE_SPACE,
      .zero_space = GREE_ZERO_SPACE,
      .max = max,
      .msb_first = false,
      .stop_bit = false,
  };

  size_t idx = 0;
  size_t n = ir_encode_pulse_distance(symbols, block1, GREE_BLOCK_BITS, &cfg);
  if (n == 0)
    return 0;
  idx += n;

  cfg.header_mark = 0;
  cfg.header_space = 0;
  cfg.max = max - idx;
  n = ir_encode_pulse_distance(symbols + idx, GREE_BLOCK_FOOTER, GREE_BLOCK_FOOTER_BITS, &cfg);
  if (n == 0)
    return 0;
  idx += n;

  idx = append_footer(symbols, idx, max);
  if (idx == 0)
    return 0;

  cfg.max = max - idx;
  n = ir_encode_pulse_distance(symbols + idx, block2, GREE_BLOCK_BITS, &cfg);
  if (n == 0)
    return 0;
  idx += n;

  idx = append_footer(symbols, idx, max);
  if (idx == 0)
    return 0;

  return idx;
}

bool ir_ac_gree_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state) {
  if (symbols == NULL || count == 0 || out_state == NULL) {
    ESP_LOGE(TAG, "decode: invalid arguments");
    return false;
  }
  if (count < GREE_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, GREE_HDR_MARK) ||
      !ir_match(symbols[0].duration1, GREE_HDR_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = GREE_ONE_SPACE,
      .zero_space = GREE_ZERO_SPACE,
      .msb_first = false,
  };
  uint32_t block1 = (uint32_t)ir_decode_pulse_distance(symbols, 1, GREE_BLOCK_BITS, &cfg);
  size_t block2_offset = 1 + GREE_BLOCK_BITS + GREE_BLOCK_FOOTER_BITS + 1;
  uint32_t block2 =
      (uint32_t)ir_decode_pulse_distance(symbols, block2_offset, GREE_BLOCK_BITS, &cfg);

  uint8_t bytes[GREE_STATE_LEN];
  for (size_t i = 0; i < 4; i++)
    bytes[i] = (block1 >> (i * 8)) & 0xFF;
  for (size_t i = 0; i < 4; i++)
    bytes[i + 4] = (block2 >> (i * 8)) & 0xFF;

  if (gree_checksum(bytes) != (bytes[GREE_STATE_LEN - 1] >> 4))
    return false;

  out_state->protocol = IR_AC_PROTO_GREE;
  out_state->power = (bytes[0] >> 3) & 1;

  switch (bytes[0] & 0x7) {
    case GREE_MODE_COOL:
      out_state->mode = IR_AC_MODE_COOL;
      break;
    case GREE_MODE_DRY:
      out_state->mode = IR_AC_MODE_DRY;
      break;
    case GREE_MODE_FAN:
      out_state->mode = IR_AC_MODE_FAN;
      break;
    case GREE_MODE_HEAT:
      out_state->mode = IR_AC_MODE_HEAT;
      break;
    case GREE_MODE_AUTO:
    default:
      out_state->mode = IR_AC_MODE_AUTO;
      break;
  }

  out_state->temp_c = (uint8_t)((bytes[1] & 0x0F) + GREE_TEMP_MIN);

  switch ((bytes[0] >> 4) & 0x3) {
    case GREE_FAN_MIN:
      out_state->fan = IR_AC_FAN_LOW;
      break;
    case GREE_FAN_MED:
      out_state->fan = IR_AC_FAN_MED;
      break;
    case GREE_FAN_MAX:
      out_state->fan = IR_AC_FAN_HIGH;
      break;
    case GREE_FAN_AUTO:
    default:
      out_state->fan = IR_AC_FAN_AUTO;
      break;
  }
  return true;
}

static uint8_t gree_checksum(const uint8_t *bytes) {
  uint8_t sum = GREE_CHECKSUM_START;
  for (size_t i = 0; i < 4; i++)
    sum += bytes[i] & 0x0F;
  for (size_t i = 4; i < GREE_STATE_LEN - 1; i++)
    sum += bytes[i] >> 4;
  return sum & 0x0F;
}

static size_t append_footer(rmt_symbol_word_t *symbols, size_t idx, size_t max) {
  if (idx + 1 > max)
    return 0;
  symbols[idx].duration0 = GREE_BIT_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = GREE_MSG_SPACE;
  symbols[idx].level1 = 0;
  return idx + 1;
}

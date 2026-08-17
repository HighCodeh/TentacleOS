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

#include "ir_ac_midea.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_AC_MIDEA";

#define MIDEA_RESET_STATE 0xA1826FFFFF62ULL
#define MIDEA_MASK48      0xFFFFFFFFFFFFULL

#define MIDEA_HEADER_SHIFT 43
#define MIDEA_HEADER_VALUE 0x14
#define MIDEA_POWER_SHIFT  39
#define MIDEA_FAN_SHIFT    35
#define MIDEA_MODE_SHIFT   32
#define MIDEA_USE_F_SHIFT  29
#define MIDEA_TEMP_SHIFT   24

#define MIDEA_MODE_COOL 0
#define MIDEA_MODE_DRY  1
#define MIDEA_MODE_AUTO 2
#define MIDEA_MODE_HEAT 3
#define MIDEA_MODE_FAN  4

#define MIDEA_FAN_AUTO 0
#define MIDEA_FAN_LOW  1
#define MIDEA_FAN_MED  2
#define MIDEA_FAN_HIGH 3

#define MIDEA_TEMP_MIN_C 17
#define MIDEA_TEMP_MAX_C 30
#define MIDEA_TEMP_MIN_F 62

#define MIDEA_MIN_SYMBOLS 49

static uint8_t reverse8(uint8_t value);
static uint8_t midea_checksum(uint64_t state);
static uint64_t set_field(uint64_t state, uint64_t mask, uint8_t shift, uint64_t value);

size_t ir_ac_midea_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max) {
  if (state == NULL || symbols == NULL || max == 0) {
    ESP_LOGE(TAG, "encode: invalid arguments");
    return 0;
  }

  uint8_t mode_code;
  switch (state->mode) {
    case IR_AC_MODE_COOL:
      mode_code = MIDEA_MODE_COOL;
      break;
    case IR_AC_MODE_DRY:
      mode_code = MIDEA_MODE_DRY;
      break;
    case IR_AC_MODE_HEAT:
      mode_code = MIDEA_MODE_HEAT;
      break;
    case IR_AC_MODE_FAN:
      mode_code = MIDEA_MODE_FAN;
      break;
    case IR_AC_MODE_AUTO:
    default:
      mode_code = MIDEA_MODE_AUTO;
      break;
  }

  uint8_t fan_code;
  switch (state->fan) {
    case IR_AC_FAN_LOW:
      fan_code = MIDEA_FAN_LOW;
      break;
    case IR_AC_FAN_MED:
      fan_code = MIDEA_FAN_MED;
      break;
    case IR_AC_FAN_HIGH:
      fan_code = MIDEA_FAN_HIGH;
      break;
    case IR_AC_FAN_AUTO:
    default:
      fan_code = MIDEA_FAN_AUTO;
      break;
  }

  uint8_t temp = state->temp_c;
  if (temp < MIDEA_TEMP_MIN_C)
    temp = MIDEA_TEMP_MIN_C;
  if (temp > MIDEA_TEMP_MAX_C)
    temp = MIDEA_TEMP_MAX_C;

  uint64_t raw = MIDEA_RESET_STATE;
  raw = set_field(raw, 0x7, MIDEA_MODE_SHIFT, mode_code);
  raw = set_field(raw, 0x3, MIDEA_FAN_SHIFT, fan_code);
  raw = set_field(raw, 0x1, MIDEA_POWER_SHIFT, state->power ? 1 : 0);
  raw = set_field(raw, 0x1, MIDEA_USE_F_SHIFT, 0);
  raw = set_field(raw, 0x1F, MIDEA_TEMP_SHIFT, (uint64_t)(temp - MIDEA_TEMP_MIN_C));
  raw = (raw & ~0xFFULL) | midea_checksum(raw);

  uint64_t inverted = (~raw) & MIDEA_MASK48;

  ir_encode_distance_cfg_t cfg = {
      .header_mark = MIDEA_HDR_MARK,
      .header_space = MIDEA_HDR_SPACE,
      .bit_mark = MIDEA_BIT_MARK,
      .one_space = MIDEA_ONE_SPACE,
      .zero_space = MIDEA_ZERO_SPACE,
      .max = max,
      .msb_first = true,
      .stop_bit = false,
  };

  size_t idx = 0;
  size_t n = ir_encode_pulse_distance(symbols, raw, MIDEA_FRAME_BITS, &cfg);
  if (n == 0)
    return 0;
  idx += n;

  if (idx + 1 > max)
    return 0;
  symbols[idx].duration0 = MIDEA_BIT_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = MIDEA_MIN_GAP;
  symbols[idx].level1 = 0;
  idx++;

  cfg.max = max - idx;
  n = ir_encode_pulse_distance(symbols + idx, inverted, MIDEA_FRAME_BITS, &cfg);
  if (n == 0)
    return 0;
  idx += n;

  if (idx + 1 > max)
    return 0;
  symbols[idx].duration0 = MIDEA_BIT_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = MIDEA_MIN_GAP;
  symbols[idx].level1 = 0;
  idx++;

  return idx;
}

bool ir_ac_midea_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state) {
  if (symbols == NULL || count == 0 || out_state == NULL) {
    ESP_LOGE(TAG, "decode: invalid arguments");
    return false;
  }
  if (count < MIDEA_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, MIDEA_HDR_MARK) ||
      !ir_match(symbols[0].duration1, MIDEA_HDR_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = MIDEA_ONE_SPACE,
      .zero_space = MIDEA_ZERO_SPACE,
      .msb_first = true,
  };
  uint64_t raw = ir_decode_pulse_distance(symbols, 1, MIDEA_FRAME_BITS, &cfg);

  if (((raw >> MIDEA_HEADER_SHIFT) & 0x1F) != MIDEA_HEADER_VALUE)
    return false;
  if ((raw & 0xFF) != midea_checksum(raw))
    return false;

  out_state->protocol = IR_AC_PROTO_MIDEA;
  out_state->power = (raw >> MIDEA_POWER_SHIFT) & 0x1;

  switch ((raw >> MIDEA_MODE_SHIFT) & 0x7) {
    case MIDEA_MODE_COOL:
      out_state->mode = IR_AC_MODE_COOL;
      break;
    case MIDEA_MODE_DRY:
      out_state->mode = IR_AC_MODE_DRY;
      break;
    case MIDEA_MODE_HEAT:
      out_state->mode = IR_AC_MODE_HEAT;
      break;
    case MIDEA_MODE_FAN:
      out_state->mode = IR_AC_MODE_FAN;
      break;
    case MIDEA_MODE_AUTO:
    default:
      out_state->mode = IR_AC_MODE_AUTO;
      break;
  }

  uint8_t temp_field = (raw >> MIDEA_TEMP_SHIFT) & 0x1F;
  if ((raw >> MIDEA_USE_F_SHIFT) & 0x1) {
    int fahrenheit = temp_field + MIDEA_TEMP_MIN_F;
    out_state->temp_c = (uint8_t)(((fahrenheit - 32) * 5 + 4) / 9);
  } else {
    out_state->temp_c = (uint8_t)(temp_field + MIDEA_TEMP_MIN_C);
  }

  switch ((raw >> MIDEA_FAN_SHIFT) & 0x3) {
    case MIDEA_FAN_LOW:
      out_state->fan = IR_AC_FAN_LOW;
      break;
    case MIDEA_FAN_MED:
      out_state->fan = IR_AC_FAN_MED;
      break;
    case MIDEA_FAN_HIGH:
      out_state->fan = IR_AC_FAN_HIGH;
      break;
    case MIDEA_FAN_AUTO:
    default:
      out_state->fan = IR_AC_FAN_AUTO;
      break;
  }
  return true;
}

static uint8_t reverse8(uint8_t value) {
  value = (uint8_t)((value & 0xF0) >> 4 | (value & 0x0F) << 4);
  value = (uint8_t)((value & 0xCC) >> 2 | (value & 0x33) << 2);
  value = (uint8_t)((value & 0xAA) >> 1 | (value & 0x55) << 1);
  return value;
}

static uint8_t midea_checksum(uint64_t state) {
  uint8_t sum = 0;
  uint64_t shifted = state;
  for (int i = 0; i < 5; i++) {
    shifted >>= 8;
    sum += reverse8((uint8_t)(shifted & 0xFF));
  }
  return reverse8((uint8_t)(256 - sum));
}

static uint64_t set_field(uint64_t state, uint64_t mask, uint8_t shift, uint64_t value) {
  return (state & ~(mask << shift)) | ((value & mask) << shift);
}

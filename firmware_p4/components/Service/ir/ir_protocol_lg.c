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

#include "ir_protocol_lg.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_LG";

static uint8_t checksum(uint16_t command) {
  uint8_t sum = 0;
  for (size_t i = 0; i < LG_CHECKSUM_NIBBLES; i++) {
    sum += (command >> (i * LG_NIBBLE_SHIFT)) & LG_NIBBLE_MASK;
  }
  return sum & LG_NIBBLE_MASK;
}

bool ir_protocol_lg_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;

  if (count <= LG_REPEAT_MAX_SYMBOLS && ir_match(symbols[0].duration0, LG_HEADER_MARK) &&
      ir_match(symbols[0].duration1, LG_REPEAT_SPACE)) {
    out_data->protocol = IR_PROTO_LG;
    out_data->repeat = true;
    return true;
  }

  if (count < LG_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, LG_HEADER_MARK) ||
      !ir_match(symbols[0].duration1, LG_HEADER_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = LG_ONE_SPACE,
      .zero_space = LG_ZERO_SPACE,
      .msb_first = true,
  };
  uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 1, LG_FRAME_BITS, &cfg);

  uint8_t addr = (raw >> LG_ADDR_SHIFT) & LG_ADDR_MASK;
  uint16_t cmd = (raw >> LG_CMD_SHIFT) & LG_CMD_MASK;
  uint8_t chk = raw & LG_NIBBLE_MASK;

  if (addr == LG_AC_SIGNATURE)
    return false;

  if (chk != checksum(cmd))
    return false;

  out_data->protocol = IR_PROTO_LG;
  out_data->address = addr;
  out_data->command = cmd;
  out_data->repeat = false;
  return true;
}

size_t ir_protocol_lg_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  if (data->repeat) {
    if (max < LG_REPEAT_SYMBOL_COUNT)
      return 0;

    symbols[0] = (rmt_symbol_word_t){
        .duration0 = LG_HEADER_MARK, .level0 = 1, .duration1 = LG_REPEAT_SPACE, .level1 = 0};
    symbols[1] =
        (rmt_symbol_word_t){.duration0 = LG_BIT_MARK, .level0 = 1, .duration1 = 0, .level1 = 0};
    return LG_REPEAT_SYMBOL_COUNT;
  }

  uint8_t chk = checksum(data->command);
  uint32_t raw = ((uint32_t)(data->address & LG_ADDR_MASK) << LG_ADDR_SHIFT) |
                 ((uint32_t)(data->command & LG_CMD_MASK) << LG_CMD_SHIFT) | (chk & LG_NIBBLE_MASK);

  ir_encode_distance_cfg_t cfg = {
      .header_mark = LG_HEADER_MARK,
      .header_space = LG_HEADER_SPACE,
      .bit_mark = LG_BIT_MARK,
      .one_space = LG_ONE_SPACE,
      .zero_space = LG_ZERO_SPACE,
      .max = max,
      .msb_first = true,
      .stop_bit = true,
  };
  return ir_encode_pulse_distance(symbols, raw, LG_FRAME_BITS, &cfg);
}
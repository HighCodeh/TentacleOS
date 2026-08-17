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

#include "ir_protocol_nec.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_NEC";

bool ir_protocol_nec_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;

  if (!ir_match(symbols[0].duration0, NEC_HEADER_MARK))
    return false;

  if (count <= NEC_REPEAT_MAX_SYMBOLS && ir_match(symbols[0].duration1, NEC_REPEAT_SPACE)) {
    out_data->protocol = IR_PROTO_NEC;
    out_data->repeat = true;
    return true;
  }

  if (count < NEC_MIN_SYMBOLS || !ir_match(symbols[0].duration1, NEC_HEADER_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = NEC_ONE_SPACE,
      .zero_space = NEC_ZERO_SPACE,
      .msb_first = false,
  };
  uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 1, NEC_FRAME_BITS, &cfg);

  uint8_t addr = (raw >> 0) & NEC_ADDR_STANDARD_MAX;
  uint8_t addr_inv = (raw >> NEC_ADDR_INV_SHIFT) & NEC_ADDR_STANDARD_MAX;
  uint8_t cmd = (raw >> NEC_CMD_SHIFT) & NEC_ADDR_STANDARD_MAX;
  uint8_t cmd_inv = (raw >> NEC_CMD_INV_SHIFT) & NEC_ADDR_STANDARD_MAX;

  out_data->protocol = IR_PROTO_NEC;
  out_data->repeat = false;
  out_data->address =
      ((uint8_t)(addr ^ addr_inv) == NEC_INTEGRITY_MASK) ? addr : (raw & NEC_EXT_ADDR_MASK);
  out_data->command = ((uint8_t)(cmd ^ cmd_inv) == NEC_INTEGRITY_MASK)
                          ? cmd
                          : (cmd | ((uint32_t)cmd_inv << NEC_CMD_HI_SHIFT));
  return true;
}

size_t ir_protocol_nec_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  if (data->repeat) {
    if (max < NEC_REPEAT_SYMBOL_COUNT)
      return 0;
    symbols[0] = (rmt_symbol_word_t){
        .duration0 = NEC_HEADER_MARK, .level0 = 1, .duration1 = NEC_REPEAT_SPACE, .level1 = 0};
    symbols[1] =
        (rmt_symbol_word_t){.duration0 = NEC_BIT_MARK, .level0 = 1, .duration1 = 0, .level1 = 0};
    return NEC_REPEAT_SYMBOL_COUNT;
  }

  uint32_t addr_field;
  if (data->address <= NEC_ADDR_STANDARD_MAX) {
    uint8_t addr = data->address & NEC_ADDR_STANDARD_MAX;
    addr_field = addr | ((uint32_t)(~addr & NEC_INTEGRITY_MASK) << NEC_CMD_HI_SHIFT);
  } else {
    addr_field = data->address & NEC_EXT_ADDR_MASK;
  }

  uint32_t cmd_field;
  if (data->command <= NEC_ADDR_STANDARD_MAX) {
    uint8_t cmd = data->command & NEC_ADDR_STANDARD_MAX;
    cmd_field = cmd | ((uint32_t)(~cmd & NEC_INTEGRITY_MASK) << NEC_CMD_HI_SHIFT);
  } else {
    cmd_field = data->command & NEC_EXT_CMD_MASK;
  }

  uint32_t raw = addr_field | (cmd_field << NEC_CMD_SHIFT);

  ir_encode_distance_cfg_t cfg = {
      .header_mark = NEC_HEADER_MARK,
      .header_space = NEC_HEADER_SPACE,
      .bit_mark = NEC_BIT_MARK,
      .one_space = NEC_ONE_SPACE,
      .zero_space = NEC_ZERO_SPACE,
      .max = max,
      .msb_first = false,
      .stop_bit = true,
  };
  return ir_encode_pulse_distance(symbols, raw, NEC_FRAME_BITS, &cfg);
}
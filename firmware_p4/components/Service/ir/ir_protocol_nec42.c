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

#include "ir_protocol_nec42.h"

#include "ir_protocol.h"
#include "ir_protocol_nec.h"

bool ir_protocol_nec42_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;

  if (count < NEC42_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, NEC_HEADER_MARK) ||
      !ir_match(symbols[0].duration1, NEC_HEADER_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = NEC_ONE_SPACE,
      .zero_space = NEC_ZERO_SPACE,
      .msb_first = false,
  };
  uint64_t raw = ir_decode_pulse_distance(symbols, 1, NEC42_FRAME_BITS, &cfg);

  uint16_t addr = raw & NEC42_ADDR_MASK;
  uint16_t addr_inv = (raw >> NEC42_ADDR_INV_SHIFT) & NEC42_ADDR_MASK;
  uint8_t cmd = (raw >> NEC42_CMD_SHIFT) & NEC42_CMD_MASK;
  uint8_t cmd_inv = (raw >> NEC42_CMD_INV_SHIFT) & NEC42_CMD_MASK;

  if (((addr ^ addr_inv) & NEC42_ADDR_MASK) != NEC42_ADDR_MASK)
    return false;
  if ((uint8_t)(cmd ^ cmd_inv) != NEC42_CMD_MASK)
    return false;

  out_data->protocol = IR_PROTO_NEC42;
  out_data->address = addr;
  out_data->command = cmd;
  out_data->repeat = false;
  return true;
}

size_t ir_protocol_nec42_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  uint16_t addr = data->address & NEC42_ADDR_MASK;
  uint8_t cmd = data->command & NEC42_CMD_MASK;

  uint64_t raw = (uint64_t)addr | ((uint64_t)(~addr & NEC42_ADDR_MASK) << NEC42_ADDR_INV_SHIFT) |
                 ((uint64_t)cmd << NEC42_CMD_SHIFT) |
                 ((uint64_t)(uint8_t)(~cmd) << NEC42_CMD_INV_SHIFT);

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
  return ir_encode_pulse_distance(symbols, raw, NEC42_FRAME_BITS, &cfg);
}

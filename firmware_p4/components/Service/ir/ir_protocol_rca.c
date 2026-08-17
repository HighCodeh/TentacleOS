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

#include "ir_protocol_rca.h"

#include "ir_protocol.h"

bool ir_protocol_rca_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;

  if (count < RCA_MIN_SYMBOLS)
    return false;
  if (!ir_match(symbols[0].duration0, RCA_HEADER_MARK) ||
      !ir_match(symbols[0].duration1, RCA_HEADER_SPACE))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = RCA_ONE_SPACE,
      .zero_space = RCA_ZERO_SPACE,
      .msb_first = true,
  };
  uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 1, RCA_FRAME_BITS, &cfg);

  uint8_t addr = (raw >> RCA_ADDR_SHIFT) & RCA_ADDR_MASK;
  uint8_t cmd = (raw >> RCA_CMD_SHIFT) & RCA_CMD_MASK;
  uint8_t addr_inv = (raw >> RCA_ADDR_INV_SHIFT) & RCA_ADDR_MASK;
  uint8_t cmd_inv = raw & RCA_CMD_MASK;

  if (((addr ^ addr_inv) & RCA_ADDR_MASK) != RCA_ADDR_INTEGRITY_MASK)
    return false;
  if ((uint8_t)(cmd ^ cmd_inv) != RCA_CMD_INTEGRITY_MASK)
    return false;

  out_data->protocol = IR_PROTO_RCA;
  out_data->address = addr;
  out_data->command = cmd;
  out_data->repeat = false;
  return true;
}

size_t ir_protocol_rca_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  uint8_t addr = data->address & RCA_ADDR_MASK;
  uint8_t cmd = data->command & RCA_CMD_MASK;

  uint32_t raw = ((uint32_t)addr << RCA_ADDR_SHIFT) | ((uint32_t)cmd << RCA_CMD_SHIFT) |
                 ((uint32_t)(~addr & RCA_ADDR_MASK) << RCA_ADDR_INV_SHIFT) |
                 ((uint32_t)(~cmd & RCA_CMD_MASK));

  ir_encode_distance_cfg_t cfg = {
      .header_mark = RCA_HEADER_MARK,
      .header_space = RCA_HEADER_SPACE,
      .bit_mark = RCA_BIT_MARK,
      .one_space = RCA_ONE_SPACE,
      .zero_space = RCA_ZERO_SPACE,
      .max = max,
      .msb_first = true,
      .stop_bit = true,
  };
  return ir_encode_pulse_distance(symbols, raw, RCA_FRAME_BITS, &cfg);
}

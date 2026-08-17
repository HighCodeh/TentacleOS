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

#include "ir_protocol_pioneer.h"

#include "ir_protocol.h"

bool ir_protocol_pioneer_decode(const rmt_symbol_word_t *symbols,
                                size_t count,
                                ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;

  if (count < PIONEER_MIN_SYMBOLS)
    return false;
  if (!ir_match_tol(symbols[0].duration0, PIONEER_HEADER_MARK, IR_TOLERANCE_STRICT) ||
      !ir_match_tol(symbols[0].duration1, PIONEER_HEADER_SPACE, IR_TOLERANCE_STRICT))
    return false;

  ir_pulse_distance_cfg_t cfg = {
      .one_space = PIONEER_ONE_SPACE,
      .zero_space = PIONEER_ZERO_SPACE,
      .msb_first = false,
  };
  uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 1, PIONEER_FRAME_BITS, &cfg);

  uint8_t addr = (raw >> 0) & PIONEER_ADDR_STANDARD_MAX;
  uint8_t addr_inv = (raw >> PIONEER_ADDR_INV_SHIFT) & PIONEER_ADDR_STANDARD_MAX;
  uint8_t cmd = (raw >> PIONEER_CMD_SHIFT) & PIONEER_ADDR_STANDARD_MAX;
  uint8_t cmd_inv = (raw >> PIONEER_CMD_INV_SHIFT) & PIONEER_ADDR_STANDARD_MAX;

  if ((uint8_t)(cmd ^ cmd_inv) != PIONEER_INTEGRITY_MASK)
    return false;

  out_data->protocol = IR_PROTO_PIONEER;
  out_data->command = cmd;
  out_data->repeat = false;
  out_data->address =
      ((uint8_t)(addr ^ addr_inv) == PIONEER_INTEGRITY_MASK) ? addr : (raw & PIONEER_EXT_ADDR_MASK);
  return true;
}

size_t ir_protocol_pioneer_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  uint8_t cmd = data->command & PIONEER_ADDR_STANDARD_MAX;
  uint32_t raw;

  if (data->address <= PIONEER_ADDR_STANDARD_MAX) {
    uint8_t addr = data->address & PIONEER_ADDR_STANDARD_MAX;
    raw = addr | ((uint32_t)(~addr & PIONEER_INTEGRITY_MASK) << PIONEER_ADDR_INV_SHIFT) |
          ((uint32_t)cmd << PIONEER_CMD_SHIFT) |
          ((uint32_t)(~cmd & PIONEER_INTEGRITY_MASK) << PIONEER_CMD_INV_SHIFT);
  } else {
    raw = (data->address & PIONEER_EXT_ADDR_MASK) | ((uint32_t)cmd << PIONEER_CMD_SHIFT) |
          ((uint32_t)(~cmd & PIONEER_INTEGRITY_MASK) << PIONEER_CMD_INV_SHIFT);
  }

  ir_encode_distance_cfg_t cfg = {
      .header_mark = PIONEER_HEADER_MARK,
      .header_space = PIONEER_HEADER_SPACE,
      .bit_mark = PIONEER_BIT_MARK,
      .one_space = PIONEER_ONE_SPACE,
      .zero_space = PIONEER_ZERO_SPACE,
      .max = max,
      .msb_first = false,
      .stop_bit = true,
  };
  return ir_encode_pulse_distance(symbols, raw, PIONEER_FRAME_BITS, &cfg);
}

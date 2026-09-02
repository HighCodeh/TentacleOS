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

#include "ir_protocol_sony.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_SONY";

bool ir_protocol_sony_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;
  if (!ir_match(symbols[0].duration1, SONY_HEADER_SPACE)) {
    ESP_LOGD(TAG, "Header space mismatch");
    return false;
  }

  size_t data_bits;
  if (count >= SONY_SIRC20_MIN_SYMBOLS)
    data_bits = SONY_SIRC20_BITS;
  else if (count >= SONY_SIRC15_MIN_SYMBOLS)
    data_bits = SONY_SIRC15_BITS;
  else if (count >= SONY_SIRC12_MIN_SYMBOLS)
    data_bits = SONY_SIRC12_BITS;
  else
    return false;

  ir_pulse_width_cfg_t cfg = {
      .one_mark = SONY_ONE_MARK,
      .zero_mark = SONY_ZERO_MARK,
      .msb_first = false,
  };
  uint64_t raw = ir_decode_pulse_width(symbols, 1, data_bits, &cfg);

  out_data->protocol = IR_PROTO_SONY;
  out_data->command = raw & SONY_CMD_MASK;
  out_data->address = (raw >> SONY_ADDR_SHIFT) & SONY_ADDR_MASK;
  out_data->repeat = false;
  return true;
}

size_t ir_protocol_sony_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  size_t num_bits = data->nbits;
  if (num_bits == 0) {
    if (data->address > SONY_SIRC15_ADDR_MAX)
      num_bits = SONY_SIRC20_BITS;
    else if (data->address > SONY_SIRC12_ADDR_MAX)
      num_bits = SONY_SIRC15_BITS;
    else
      num_bits = SONY_SIRC12_BITS;
  }

  uint64_t raw = (data->command & SONY_CMD_MASK) | ((uint64_t)(data->address) << SONY_ADDR_SHIFT);

  ir_encode_width_cfg_t cfg = {
      .header_mark = SONY_HEADER_MARK,
      .header_space = SONY_HEADER_SPACE,
      .one_mark = SONY_ONE_MARK,
      .zero_mark = SONY_ZERO_MARK,
      .bit_space = SONY_BIT_SPACE,
      .max = max,
      .msb_first = false,
      .stop_bit = false,
  };
  return ir_encode_pulse_width(symbols, raw, num_bits, &cfg);
}

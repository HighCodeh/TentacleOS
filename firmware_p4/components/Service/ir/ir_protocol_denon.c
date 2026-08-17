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

#include "ir_protocol_denon.h"

#include "esp_log.h"

#include "ir_protocol.h"

static const char *TAG = "IR_DENON";

bool ir_protocol_denon_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;

  if (count < DENON_MIN_SYMBOLS)
    return false;
  if (symbols[0].duration0 > DENON_MARK_MAX_US)
    return false;

  for (size_t i = 0; i < DENON_VALIDATE_MARK_COUNT; i++) {
    if (!ir_match(symbols[i].duration0, DENON_BIT_MARK)) {
      ESP_LOGD(TAG, "Mark validation failed at symbol %d", i);
      return false;
    }
  }

  ir_pulse_distance_cfg_t cfg = {
      .one_space = DENON_ONE_SPACE,
      .zero_space = DENON_ZERO_SPACE,
      .msb_first = false,
  };
  uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 0, DENON_FRAME_BITS, &cfg);

  uint8_t frame = (raw >> DENON_FRAME_SHIFT) & DENON_FRAME_MASK;
  uint8_t cmd = (raw >> DENON_CMD_SHIFT) & 0xFF;

  out_data->protocol = IR_PROTO_DENON;
  out_data->address = raw & DENON_ADDR_MASK;
  if (frame == DENON_FRAME_INVERTED) {
    out_data->command = (uint8_t)~cmd;
    out_data->repeat = true;
  } else {
    out_data->command = cmd;
    out_data->repeat = false;
  }
  return true;
}

size_t ir_protocol_denon_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  uint16_t frame1 =
      (data->address & DENON_ADDR_MASK) | ((uint16_t)(data->command & 0xFF) << DENON_CMD_SHIFT);
  uint16_t frame2 = frame1 ^ DENON_INVERT_MASK;

  ir_encode_distance_cfg_t cfg = {
      .header_mark = 0,
      .header_space = 0,
      .bit_mark = DENON_BIT_MARK,
      .one_space = DENON_ONE_SPACE,
      .zero_space = DENON_ZERO_SPACE,
      .max = max,
      .msb_first = false,
      .stop_bit = true,
  };

  size_t n1 = ir_encode_pulse_distance(symbols, frame1, DENON_FRAME_BITS, &cfg);
  if (n1 == 0)
    return 0;

  if (n1 + 1 > max)
    return 0;
  symbols[n1].duration0 = DENON_REPEAT_GAP_US / 2;
  symbols[n1].level0 = 0;
  symbols[n1].duration1 = DENON_REPEAT_GAP_US / 2;
  symbols[n1].level1 = 0;
  size_t idx = n1 + 1;

  cfg.max = max - idx;
  size_t n2 = ir_encode_pulse_distance(symbols + idx, frame2, DENON_FRAME_BITS, &cfg);
  if (n2 == 0)
    return 0;

  return idx + n2;
}
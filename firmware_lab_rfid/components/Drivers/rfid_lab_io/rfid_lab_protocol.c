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

#include "rfid_lab_protocol.h"

#include <string.h>

#define HEADER_BITS 9U
#define DATA_ROWS 10U
#define NIBBLE_BITS 4U
#define ROW_BITS 5U
#define HEADER_MASK 0x1FFU
#define NIBBLE_MASK 0xFU
#define SHORT_MIN_US 156U
#define SHORT_MAX_US 356U
#define LONG_MIN_US 412U
#define LONG_MAX_US 612U
#define FRAME_MASK UINT64_C(0xFFFFFFFFFFFFFFFF)

uint64_t rfid_lab_protocol_encode(uint64_t id) {
  uint64_t frame = HEADER_MASK;
  uint8_t columns = 0;
  for (uint32_t row = 0; row < DATA_ROWS; row++) {
    uint8_t nibble = (id >> ((DATA_ROWS - 1U - row) * NIBBLE_BITS)) & NIBBLE_MASK;
    uint8_t parity = 0;
    columns ^= nibble;
    for (uint32_t bit = 0; bit < NIBBLE_BITS; bit++) {
      uint8_t value = (nibble >> (NIBBLE_BITS - 1U - bit)) & 1U;
      frame = (frame << 1U) | value;
      parity ^= value;
    }
    frame = (frame << 1U) | parity;
  }
  return ((frame << NIBBLE_BITS) | columns) << 1U;
}

bool rfid_lab_protocol_validate(uint64_t frame, uint64_t *out_id) {
  if (out_id == NULL || (frame >> (RFID_LAB_FRAME_BITS - HEADER_BITS)) != HEADER_MASK ||
      (frame & 1U) != 0) return false;
  uint64_t id = 0;
  uint8_t columns = 0;
  for (uint32_t row = 0; row < DATA_ROWS; row++) {
    uint32_t shift = (DATA_ROWS - 1U - row) * ROW_BITS + ROW_BITS;
    uint8_t word = (frame >> shift) & ((1U << ROW_BITS) - 1U);
    uint8_t nibble = word >> 1U;
    uint8_t parity = word & 1U;
    for (uint32_t bit = 0; bit < NIBBLE_BITS; bit++) parity ^= (nibble >> bit) & 1U;
    if (parity != 0) return false;
    columns ^= nibble;
    id = (id << NIBBLE_BITS) | nibble;
  }
  if (((frame >> 1U) & NIBBLE_MASK) != columns) return false;
  *out_id = id;
  return true;
}

void rfid_lab_protocol_reset(rfid_lab_decoder_t *decoder) {
  memset(decoder, 0, sizeof(*decoder));
}

static void feed_half(rfid_lab_decoder_t *decoder, uint8_t level) {
  for (uint32_t phase = 0; phase < 2U; phase++) {
    rfid_lab_decoder_lane_t *lane = &decoder->lanes[phase];
    if (decoder->halves == 0 && phase == 1U) continue;
    if (!lane->has_first) {
      lane->first_level = level;
      lane->has_first = true;
      continue;
    }
    lane->has_first = false;
    if (lane->first_level == level) {
      lane->bits = 0;
      lane->shift = 0;
      lane->repeats = 0;
      continue;
    }
    lane->shift = (lane->shift << 1U) | lane->first_level;
    lane->bits++;
    if (lane->bits < RFID_LAB_FRAME_BITS) continue;
    uint64_t id;
    bool is_frame = rfid_lab_protocol_validate(lane->shift, &id);
    if (!is_frame) is_frame = rfid_lab_protocol_validate(lane->shift ^ FRAME_MASK, &id);
    if (!is_frame) continue;
    if (lane->repeats != 0 && lane->candidate == id &&
        lane->bits - lane->last_match == RFID_LAB_FRAME_BITS) lane->repeats++;
    else lane->repeats = 1;
    lane->candidate = id;
    lane->last_match = lane->bits;
    if (lane->repeats >= RFID_LAB_CONFIRM_FRAMES) {
      decoder->id = id;
      decoder->is_valid = true;
      lane->repeats = RFID_LAB_CONFIRM_FRAMES;
    }
  }
  decoder->halves++;
}

void rfid_lab_protocol_feed(rfid_lab_decoder_t *decoder, uint32_t duration_us, bool level) {
  uint32_t halves;
  if (duration_us >= SHORT_MIN_US && duration_us <= SHORT_MAX_US) halves = 1;
  else if (duration_us >= LONG_MIN_US && duration_us <= LONG_MAX_US) halves = 2;
  else {
    memset(decoder->lanes, 0, sizeof(decoder->lanes));
    decoder->halves = 0;
    decoder->invalid_pulses++;
    return;
  }
  for (uint32_t i = 0; i < halves; i++) feed_half(decoder, level);
}

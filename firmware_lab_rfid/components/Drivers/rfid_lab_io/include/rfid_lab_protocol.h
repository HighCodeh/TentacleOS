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

#ifndef RFID_LAB_PROTOCOL_H
#define RFID_LAB_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RFID_LAB_FRAME_BITS 64U
#define RFID_LAB_ID_BYTES 5U
#define RFID_LAB_HALF_US 256U
#define RFID_LAB_CONFIRM_FRAMES 3U

typedef struct {
  uint64_t shift;
  uint64_t candidate;
  uint32_t bits;
  uint32_t last_match;
  uint8_t first_level;
  uint8_t repeats;
  bool has_first;
} rfid_lab_decoder_lane_t;
typedef struct {
  rfid_lab_decoder_lane_t lanes[2];
  uint32_t halves;
  uint32_t invalid_pulses;
  uint64_t id;
  bool is_valid;
} rfid_lab_decoder_t;
uint64_t rfid_lab_protocol_encode(uint64_t id);
bool rfid_lab_protocol_validate(uint64_t frame, uint64_t *out_id);
void rfid_lab_protocol_reset(rfid_lab_decoder_t *decoder);
void rfid_lab_protocol_feed(rfid_lab_decoder_t *decoder, uint32_t duration_us, bool level);
#ifdef __cplusplus
}
#endif
#endif

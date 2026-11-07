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

#include "texkom.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#define TEXKOM_BYTE_BITS         8U
#define TEXKOM_FRAME_BITS        64U
#define TEXKOM_TK13_MARKER       0x63U
#define TEXKOM_TK17_MARKER       0xCAU
#define TEXKOM_PREAMBLE          0xFFU
#define TEXKOM_TYPE_OFFSET       2U
#define TEXKOM_UID_OFFSET        3U
#define TEXKOM_CRC_OFFSET        7U
#define TEXKOM_CRC_POLY          0x31U
#define TEXKOM_DALLAS_POLY       0x8CU
#define TEXKOM_HIGH_BIT          0x80U
#define TEXKOM_NIBBLE_MASK       0x0FU
#define TEXKOM_NIBBLE_BITS       4U
#define TEXKOM_PAIR_BITS         2U
#define TEXKOM_PAIRS_PER_BYTE    4U
#define TEXKOM_SHARE_SCALE       100U
#define TEXKOM_SHARE_MIN         10U
#define TEXKOM_SHARE_LOW         30U
#define TEXKOM_SHARE_MID         50U
#define TEXKOM_SHARE_HIGH        70U
#define TEXKOM_SHARE_MAX         90U
#define TEXKOM_TOLERANCE_DIVISOR 3U
#define TEXKOM_CARRIER_DIVISOR   64U
#define TEXKOM_LONG_LOW          17U
#define TEXKOM_SHORT_LOW         7U
#define TEXKOM_TAIL_LOW          10U

static const char *TAG = "TEXKOM";

static uint32_t distance(uint32_t a, uint32_t b) {
  return a > b ? a - b : b - a;
}

static bool valid_checksum(const uint8_t *frame, texkom_type_t type) {
  uint8_t crc = 0;
  if (type == TEXKOM_TYPE_MMBIT) {
    crc = TEXKOM_NIBBLE_MASK;
    for (size_t i = TEXKOM_UID_OFFSET + 1; i < TEXKOM_CRC_OFFSET; i++)
      crc ^= (frame[i] & TEXKOM_NIBBLE_MASK) ^ (frame[i] >> TEXKOM_NIBBLE_BITS);
    return (uint8_t)((crc & TEXKOM_NIBBLE_MASK) << TEXKOM_NIBBLE_BITS) ==
           (frame[TEXKOM_CRC_OFFSET] & (uint8_t)~TEXKOM_NIBBLE_MASK);
  }
  for (size_t i = TEXKOM_UID_OFFSET; i < TEXKOM_CRC_OFFSET; i++) {
    crc ^= frame[i];
    for (size_t bit = 0; bit < TEXKOM_BYTE_BITS; bit++) {
      if (type == TEXKOM_TYPE_TK17)
        crc = (crc >> 1) ^ ((crc & 1U) ? TEXKOM_DALLAS_POLY : 0);
      else
        crc = (uint8_t)(crc << 1) ^ ((crc & TEXKOM_HIGH_BIT) ? TEXKOM_CRC_POLY : 0);
    }
  }
  return crc == frame[TEXKOM_CRC_OFFSET];
}

static bool decode_pairs(const uint16_t *gaps, uint8_t *frame, bool relative) {
  uint16_t low = UINT16_MAX, high = 0;
  for (size_t i = 0; i < TEXKOM_MAX_GAPS; i++) {
    if (gaps[i] < low)
      low = gaps[i];
    if (gaps[i] > high)
      high = gaps[i];
  }
  if (low == 0 || high == low)
    return false;
  uint32_t split = ((uint32_t)high + low) / TEXKOM_PAIR_BITS;
  uint32_t sums[2] = {0}, counts[2] = {0};
  for (size_t i = 0; i < TEXKOM_MAX_GAPS; i++) {
    size_t side = gaps[i] > split;
    sums[side] += gaps[i];
    counts[side]++;
  }
  if (counts[0] == 0 || counts[1] == 0)
    return false;
  uint32_t short_gap = sums[0] / counts[0], long_gap = sums[1] / counts[1];
  uint32_t tolerance = (long_gap - short_gap) / TEXKOM_TOLERANCE_DIVISOR + 1;
  memset(frame, 0, TEXKOM_FRAME_SIZE);
  for (size_t bit = 0; bit < TEXKOM_FRAME_BITS; bit++) {
    size_t i = bit * TEXKOM_PAIR_BITS;
    bool value;
    if (relative) {
      if (i + 1 < TEXKOM_MAX_GAPS) {
        if (gaps[i] == gaps[i + 1])
          return false;
        value = gaps[i] > gaps[i + 1];
      } else {
        bool previous = gaps[i - TEXKOM_PAIR_BITS] > gaps[i - 1];
        bool same = distance(gaps[i], gaps[i - TEXKOM_PAIR_BITS]) < distance(gaps[i], gaps[i - 1]);
        value = previous == same;
      }
    } else {
      if (distance(gaps[i], long_gap) < tolerance)
        value = true;
      else if (distance(gaps[i], short_gap) < tolerance)
        value = false;
      else
        return false;
      if (i + 1 < TEXKOM_MAX_GAPS &&
          distance(gaps[i + 1], value ? short_gap : long_gap) >= tolerance)
        return false;
    }
    frame[bit / TEXKOM_BYTE_BITS] = (uint8_t)((frame[bit / TEXKOM_BYTE_BITS] << 1) | value);
  }
  return true;
}

esp_err_t texkom_decode(const uint16_t *gaps, size_t count, texkom_data_t *out_data) {
  if (gaps == NULL || out_data == NULL || (count != TEXKOM_MAX_GAPS && count != TEXKOM_TK17_GAPS))
    return ESP_ERR_INVALID_ARG;
  texkom_data_t data = {0};
  if (count == TEXKOM_TK17_GAPS) {
    data.type = TEXKOM_TYPE_TK17;
    for (size_t i = 0; i < count; i += TEXKOM_PAIR_BITS) {
      uint32_t total = (uint32_t)gaps[i] + gaps[i + 1];
      if (total == 0)
        return ESP_ERR_INVALID_RESPONSE;
      uint32_t share = gaps[i + 1] * TEXKOM_SHARE_SCALE / total;
      if (share < TEXKOM_SHARE_MIN || share > TEXKOM_SHARE_MAX)
        return ESP_ERR_INVALID_RESPONSE;
      uint8_t pair = share < TEXKOM_SHARE_LOW    ? 0
                     : share < TEXKOM_SHARE_MID  ? 2
                     : share < TEXKOM_SHARE_HIGH ? 1
                                                 : 3;
      data.frame[i / TEXKOM_BYTE_BITS] |= pair << (i % TEXKOM_BYTE_BITS);
    }
    if (data.frame[TEXKOM_TYPE_OFFSET] != TEXKOM_TK17_MARKER)
      return ESP_ERR_INVALID_RESPONSE;
  } else {
    if (decode_pairs(gaps, data.frame, false))
      data.type = TEXKOM_TYPE_TK13;
    else if (decode_pairs(gaps, data.frame, true))
      data.type = TEXKOM_TYPE_TK15;
    else
      return ESP_ERR_INVALID_RESPONSE;
    if (data.frame[TEXKOM_TYPE_OFFSET] == TEXKOM_PREAMBLE &&
        data.frame[TEXKOM_UID_OFFSET] == TEXKOM_PREAMBLE)
      data.type = TEXKOM_TYPE_MMBIT;
    else if (data.frame[TEXKOM_TYPE_OFFSET] != TEXKOM_TK13_MARKER)
      return ESP_ERR_INVALID_RESPONSE;
  }
  if (data.frame[0] != TEXKOM_PREAMBLE || data.frame[1] != TEXKOM_PREAMBLE ||
      !valid_checksum(data.frame, data.type))
    return ESP_ERR_INVALID_RESPONSE;
  *out_data = data;
  ESP_LOGD(TAG, "Decoded %s", texkom_type_name(data.type));
  return ESP_OK;
}

const char *texkom_type_name(texkom_type_t type) {
  static const char *const NAMES[] = {"TK13", "TK15", "TK17", "MMBIT"};
  return (unsigned)type < TEXKOM_TYPE_COUNT ? NAMES[type] : "Unknown";
}

esp_err_t texkom_encode(const texkom_data_t *data, uint16_t *out_cycles, size_t *out_count) {
  if (data == NULL || out_cycles == NULL || out_count == NULL || *out_count < TEXKOM_MAX_GAPS + 1 ||
      (unsigned)data->type >= TEXKOM_TYPE_COUNT)
    return ESP_ERR_INVALID_ARG;
  if (data->frame[0] != TEXKOM_PREAMBLE || data->frame[1] != TEXKOM_PREAMBLE ||
      (data->type == TEXKOM_TYPE_MMBIT
           ? (data->frame[2] != TEXKOM_PREAMBLE || data->frame[3] != TEXKOM_PREAMBLE)
           : data->frame[TEXKOM_TYPE_OFFSET] !=
                 (data->type == TEXKOM_TYPE_TK17 ? TEXKOM_TK17_MARKER : TEXKOM_TK13_MARKER)) ||
      !valid_checksum(data->frame, data->type))
    return ESP_ERR_INVALID_RESPONSE;
  size_t count = 0;
  if (data->type == TEXKOM_TYPE_TK17) {
    static const uint8_t LOWS[][2] = {{25, 5}, {12, 18}, {17, 13}, {7, 23}};
    for (size_t byte = 0; byte < TEXKOM_FRAME_SIZE; byte++) {
      for (size_t shift = 0; shift < TEXKOM_BYTE_BITS; shift += TEXKOM_PAIR_BITS) {
        uint8_t pair = (data->frame[byte] >> shift) & (TEXKOM_PAIRS_PER_BYTE - 1);
        out_cycles[count++] = LOWS[pair][0] * TEXKOM_CARRIER_DIVISOR;
        out_cycles[count++] = LOWS[pair][1] * TEXKOM_CARRIER_DIVISOR;
      }
    }
    out_cycles[count++] = TEXKOM_TAIL_LOW * TEXKOM_CARRIER_DIVISOR;
  } else {
    for (size_t bit = 0; bit < TEXKOM_FRAME_BITS; bit++) {
      bool value =
          (data->frame[bit / TEXKOM_BYTE_BITS] >> (TEXKOM_BYTE_BITS - 1 - bit % TEXKOM_BYTE_BITS)) &
          1U;
      out_cycles[count++] = (value ? TEXKOM_LONG_LOW : TEXKOM_SHORT_LOW) * TEXKOM_CARRIER_DIVISOR;
      out_cycles[count++] = (value ? TEXKOM_SHORT_LOW : TEXKOM_LONG_LOW) * TEXKOM_CARRIER_DIVISOR;
    }
  }
  *out_count = count;
  return ESP_OK;
}

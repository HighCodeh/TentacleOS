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

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "expected.h"

#define TAG android_tag
#include "android_spam.c"
#undef TAG
#define TAG apple_tag
#include "apple_juice_spam.c"
#undef TAG
#define TAG samsung_tag
#include "samsung_spam.c"
#undef TAG

#define TAG swift_tag
#include "swift_pair_spam.c"
#undef TAG

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define GUARD_BYTE 0xA5
#define TEST_BUFFER_SIZE 33
#define ADVERTISING_LIMIT 31
#define TEST_BYTE_BITS 8
#define TEST_MODEL_HIGH_BITS 16

typedef int (*payload_cb_t)(uint8_t *, size_t);
static uint32_t s_random_values[32];
static size_t s_random_pos;

uint32_t esp_random(void) {
  assert(s_random_pos < ARRAY_COUNT(s_random_values));
  return s_random_values[s_random_pos++];
}

static void reset_random(uint32_t first, uint32_t second) {
  s_random_pos = 0;
  s_random_values[0] = first;
  s_random_values[1] = second;
  s_random_values[2] = 0;
}

static void check_bounds(payload_cb_t generate, uint32_t first, uint32_t second,
                         size_t length, const uint8_t *expected) {
  uint8_t buffer[TEST_BUFFER_SIZE];
  reset_random(first, second);
  assert(generate(NULL, ADVERTISING_LIMIT) == 0);
  for (size_t size = 0; size < length; ++size) {
    memset(buffer, GUARD_BYTE, sizeof(buffer));
    reset_random(first, second);
    assert(generate(buffer + 1, size) == 0);
    for (size_t i = 0; i < sizeof(buffer); ++i)
      assert(buffer[i] == GUARD_BYTE);
  }
  for (size_t size = length; size <= ADVERTISING_LIMIT; ++size) {
    memset(buffer, GUARD_BYTE, sizeof(buffer));
    reset_random(first, second);
    assert(generate(buffer + 1, size) == (int)length);
    assert(buffer[0] == GUARD_BYTE);
    assert(memcmp(buffer + 1, expected, length) == 0);
    for (size_t i = length + 1; i < sizeof(buffer); ++i)
      assert(buffer[i] == GUARD_BYTE);
  }
}

static void check_swift_pair(void) {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (size_t seed = 0; seed < sizeof(alphabet) - 1; ++seed) {
    for (size_t capacity = 0; capacity < TEST_BUFFER_SIZE; ++capacity) {
      uint8_t buffer[TEST_BUFFER_SIZE + 1];
      memset(buffer, GUARD_BYTE, sizeof(buffer));
      reset_random(seed, seed);
      for (size_t i = 1; i < ARRAY_COUNT(s_random_values); ++i)
        s_random_values[i] = seed;
      int length = generate_swift_pair_payload(buffer + 1, capacity);
      assert(buffer[0] == GUARD_BYTE);
      if (capacity < 7) {
        assert(length == 0);
      } else {
        size_t name_len = seed % 10 + 1;
        if (name_len > capacity - 7)
          name_len = capacity - 7;
        assert(length == (int)(7 + name_len));
        uint8_t header[] = {6 + name_len, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x80};
        assert(memcmp(buffer + 1, header, sizeof(header)) == 0);
        for (size_t i = 0; i < name_len; ++i)
          assert(buffer[1 + sizeof(header) + i] == alphabet[seed]);
      }
      for (size_t i = 1 + length; i < sizeof(buffer); ++i)
        assert(buffer[i] == GUARD_BYTE);
    }
  }
  reset_random(0, 0);
  assert(generate_swift_pair_payload(NULL, ADVERTISING_LIMIT) == 0);
}

int main(void) {
  check_swift_pair();
  assert(sizeof(ANDROID_MODELS) == ARRAY_COUNT(expected_fastpair) * 3);
  assert(ANDROID_MODELS_COUNT == ARRAY_COUNT(expected_fastpair));
  assert(WATCH_MODELS_COUNT == ARRAY_COUNT(expected_watch));
  assert(BUDS_MODELS_COUNT == ARRAY_COUNT(expected_buds));
  assert(IOS1_DEVICE_TYPES_COUNT == ARRAY_COUNT(expected_apple));
  for (size_t i = 0; i < ARRAY_COUNT(expected_fastpair); ++i) {
    uint32_t model = expected_fastpair[i];
    uint8_t packet[] = {0x03, 0x03, 0x2C, 0xFE, 0x06, 0x16, 0x2C, 0xFE,
                        model >> TEST_MODEL_HIGH_BITS, model >> TEST_BYTE_BITS, model,
                        0x02, 0x0A, (uint8_t)-100};
    check_bounds(generate_android_payload, i, 0, sizeof(packet), packet);
  }
  for (size_t i = 0; i < ARRAY_COUNT(expected_watch); ++i) {
    uint8_t packet[] = {0x0E, 0xFF, 0x75, 0x00, 0x01, 0x00, 0x02, 0x00,
                        0x01, 0x01, 0xFF, 0x00, 0x00, 0x43, expected_watch[i]};
    check_bounds(generate_samsung_payload, 0, i, sizeof(packet), packet);
  }
  for (size_t i = 0; i < ARRAY_COUNT(expected_buds); ++i) {
    uint32_t model = expected_buds[i];
    uint8_t packet[] = {0x1B, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x81, 0x02, 0x14,
                        0x15, 0x03, 0x21, 0x01, 0x09, model >> TEST_MODEL_HIGH_BITS,
                        model >> TEST_BYTE_BITS, 0x01, model, 0x06, 0x3C, 0x94, 0x8E,
                        0x00, 0x00, 0x00, 0x00, 0xC7, 0x00, 0x10, 0xFF, 0x75};
    check_bounds(generate_samsung_payload, 1, i, sizeof(packet), packet);
  }
  for (size_t i = 0; i < ARRAY_COUNT(expected_apple); ++i) {
    uint32_t model = expected_apple[i];
    uint8_t prefix = (model == 0x0055 || model == 0x0030) ? 0x05 : 0x07;
    uint8_t packet[] = {0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19, prefix,
                        model >> TEST_BYTE_BITS, model, 0x75, 0xAA, 0x30, 0x01,
                        0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    check_bounds(generate_apple_juice_payload, 0, i, sizeof(packet), packet);
  }
  static const uint8_t actions[] = {
      0x01, 0x06, 0x20, 0x2B, 0xC0, 0x0D, 0x13, 0x27, 0x0B, 0x09, 0x02, 0x1E, 0x24};
  for (size_t i = 0; i < ARRAY_COUNT(actions); ++i) {
    uint8_t packet[] = {0x16, 0xFF, 0x4C, 0x00, 0x04, 0x04, 0x2A, 0x00,
                        0x00, 0x00, 0x0F, 0x05, 0xC1, actions[i], 0x60, 0x4C,
                        0x95, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00};
    check_bounds(generate_apple_juice_payload, 1, i, sizeof(packet), packet);
  }
  return 0;
}

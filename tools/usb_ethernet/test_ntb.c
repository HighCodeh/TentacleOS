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

#include "usb_ethernet_ntb.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static unsigned calls;
static uint8_t expected[USB_ETHERNET_FRAME_MAX];
static bool accept(const uint8_t *data, size_t length, void *context) {
  (void)context;
  (void)data;
  assert(length >= USB_ETHERNET_FRAME_MIN && length <= sizeof(expected));
  ++calls;
  return true;
}
static bool compare(const uint8_t *data, size_t length, void *context) {
  (void)context;
  assert(memcmp(data, expected, length) == 0);
  ++calls;
  return true;
}
static void put16(uint8_t *data, uint16_t value) { data[0] = value; data[1] = value >> 8; }
int main(void) {
  uint8_t buffer[USB_ETHERNET_NTB_SIZE], original[USB_ETHERNET_NTB_SIZE];
  for (size_t i = 0; i < sizeof(expected); ++i) expected[i] = (uint8_t)i;
  for (size_t length = USB_ETHERNET_FRAME_MIN; length <= sizeof(expected); ++length) {
    for (unsigned remainder = 0; remainder < 4; ++remainder) {
      size_t size = usb_ethernet_ntb_encode(buffer, sizeof(buffer), expected, length,
                                           UINT16_MAX, 4, remainder, 4);
      assert(size != 0);
      calls = 0;
      assert(usb_ethernet_ntb_decode(buffer, size, compare, NULL));
      assert(calls == 1);
    }
  }
  size_t size = usb_ethernet_ntb_encode(original, sizeof(original), expected, sizeof(expected), 0, 4, 0, 4);
  for (size_t cut = 0; cut < size; ++cut) {
    calls = 0;
    assert(!usb_ethernet_ntb_decode(original, cut, accept, NULL));
    assert(calls == 0);
  }
  memcpy(buffer, original, size);
  put16(buffer + 18, 12); // Cyclic NDP chain.
  calls = 0;
  assert(!usb_ethernet_ntb_decode(buffer, size, accept, NULL) && calls == 0);
  memcpy(buffer, original, size);
  put16(buffer + 20, 12); // Frame overlaps its NDP.
  assert(!usb_ethernet_ntb_decode(buffer, size, accept, NULL));
  memcpy(buffer, original, size);
  put16(buffer + 22, UINT16_MAX); // Out-of-bounds datagram.
  assert(!usb_ethernet_ntb_decode(buffer, size, accept, NULL));
  memcpy(buffer, original, size);
  buffer[15] = '1'; // CRC-bearing NDP is not negotiated.
  assert(!usb_ethernet_ntb_decode(buffer, size, accept, NULL));
  assert(usb_ethernet_ntb_encode(buffer, 16, expected, sizeof(expected), 0, 4, 0, 4) == 0);
  assert(usb_ethernet_ntb_encode(buffer, sizeof(buffer), expected, sizeof(expected), 0, 0, 0, 4) == 0);
  assert(usb_ethernet_ntb_encode(buffer, sizeof(buffer), expected, sizeof(expected), 0, 4, 4, 4) == 0);
  assert(usb_ethernet_ntb_encode(buffer, sizeof(buffer), expected, sizeof(expected), 0, 4, 0, 0) == 0);
  // Deterministic malformed packet corpus; sanitizers check every decoder access.
  uint32_t seed = 1;
  for (unsigned iteration = 0; iteration < 20000; ++iteration) {
    memcpy(buffer, original, size);
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      seed = seed * 1664525u + 1013904223u;
      size_t at = seed % size;
      seed = seed * 1664525u + 1013904223u;
      buffer[at] = seed >> 24;
    }
    (void)usb_ethernet_ntb_decode(buffer, size, accept, NULL);
  }
  puts("NCM NTB16 bounds, round trips, malformed chains and fuzz corpus: OK");
}

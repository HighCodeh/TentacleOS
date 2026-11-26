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

#include <string.h>

#define NTH_SIGNATURE           UINT32_C(0x484d434e)
#define NDP_SIGNATURE           UINT32_C(0x304d434e)
#define NTH_SIZE                12
#define NDP_SIZE                8
#define ENTRY_SIZE              4
#define NDP_SINGLE_SIZE         16
#define MAX_TABLES              16
#define MAX_DATAGRAMS           64
#define ALIGNMENT_MAX           512
#define USB_BULK_FS_PACKET_SIZE 64

static uint16_t get16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}
static void put16(uint8_t *p, uint16_t value) {
  p[0] = value;
  p[1] = value >> 8;
}
static void put32(uint8_t *p, uint32_t value) {
  put16(p, value);
  put16(p + 2, value >> 16);
}
static bool overlaps(size_t a, size_t an, size_t b, size_t bn) {
  return a < b + bn && b < a + an;
}

bool usb_ethernet_ntb_decode(const uint8_t *data,
                             size_t length,
                             usb_ethernet_frame_cb_t callback,
                             void *context) {
  if (data == NULL || callback == NULL || length < NTH_SIZE || length > UINT16_MAX ||
      get32(data) != NTH_SIGNATURE || get16(data + 4) != NTH_SIZE || get16(data + 8) != length)
    return false;
  uint16_t tables[MAX_TABLES], sizes[MAX_TABLES];
  uint16_t offsets[MAX_DATAGRAMS], lengths[MAX_DATAGRAMS];
  size_t table_count = 0, count = 0;
  uint16_t table = get16(data + 10);
  while (table != 0) {
    if (table_count == MAX_TABLES || table < NTH_SIZE || table > length - NDP_SIZE ||
        (table % ENTRY_SIZE) != 0 || get32(data + table) != NDP_SIGNATURE)
      return false;
    uint16_t size = get16(data + table + 4);
    if (size < NDP_SINGLE_SIZE || size % ENTRY_SIZE || size > length - table)
      return false;
    for (size_t i = 0; i < table_count; ++i)
      if (overlaps(table, size, tables[i], sizes[i]))
        return false;
    tables[table_count] = table;
    sizes[table_count++] = size;
    bool terminated = false;
    for (size_t p = table + NDP_SIZE; p < table + size; p += ENTRY_SIZE) {
      uint16_t offset = get16(data + p), bytes = get16(data + p + 2);
      if (offset == 0 && bytes == 0) {
        terminated = true;
        break;
      }
      if (offset < NTH_SIZE || bytes < USB_ETHERNET_FRAME_MIN || bytes > USB_ETHERNET_FRAME_MAX ||
          offset > length || bytes > length - offset || count == MAX_DATAGRAMS)
        return false;
      for (size_t i = 0; i < count; ++i)
        if (overlaps(offset, bytes, offsets[i], lengths[i]))
          return false;
      offsets[count] = offset;
      lengths[count++] = bytes;
    }
    if (!terminated)
      return false;
    table = get16(data + table + 6);
  }
  for (size_t i = 0; i < count; ++i)
    for (size_t j = 0; j < table_count; ++j)
      if (overlaps(offsets[i], lengths[i], tables[j], sizes[j]))
        return false;
  for (size_t i = 0; i < count; ++i)
    if (!callback(data + offsets[i], lengths[i], context))
      return false;
  return table_count != 0;
}

size_t usb_ethernet_ntb_encode(uint8_t *out,
                               size_t capacity,
                               const uint8_t *frame,
                               size_t length,
                               uint16_t sequence,
                               uint16_t divisor,
                               uint16_t remainder,
                               uint16_t alignment) {
  if (out == NULL || frame == NULL || length < USB_ETHERNET_FRAME_MIN ||
      length > USB_ETHERNET_FRAME_MAX || divisor == 0 || divisor > ALIGNMENT_MAX ||
      remainder >= divisor || alignment == 0 || alignment > ALIGNMENT_MAX)
    return 0;
  size_t table = (NTH_SIZE + alignment - 1) / alignment * alignment;
  size_t offset = table + NDP_SINGLE_SIZE;
  // NCM alignment applies to the IP payload, after the Ethernet header.
  offset += (remainder + divisor - ((offset + USB_ETHERNET_FRAME_MIN) % divisor)) % divisor;
  size_t total = offset + length;
  // CDC bulk TX has no ZLP flag. End the NTB with a short packet at either bus speed.
  if (total % USB_BULK_FS_PACKET_SIZE == 0)
    ++total;
  if (total > capacity || total > UINT16_MAX)
    return 0;
  memset(out, 0, total);
  put32(out, NTH_SIGNATURE);
  put16(out + 4, NTH_SIZE);
  put16(out + 6, sequence);
  put16(out + 8, total);
  put16(out + 10, table);
  put32(out + table, NDP_SIGNATURE);
  put16(out + table + 4, NDP_SINGLE_SIZE);
  put16(out + table + NDP_SIZE, offset);
  put16(out + table + NDP_SIZE + 2, length);
  memcpy(out + offset, frame, length);
  return total;
}

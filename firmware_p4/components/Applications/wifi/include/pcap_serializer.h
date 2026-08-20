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

#ifndef PCAP_SERIALIZER_H
#define PCAP_SERIALIZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PCAP_MAGIC_NUMBER              0xa1b2c3d4
#define PCAP_VERSION_MAJOR             2
#define PCAP_VERSION_MINOR             4
#define PCAP_LINK_TYPE_802_11          105
#define PCAP_LINK_TYPE_802_11_RADIOTAP 127

/**
 * @brief PCAP global file header.
 */
typedef struct {
  uint32_t magic_number;
  uint16_t version_major;
  uint16_t version_minor;
  int32_t thiszone;
  uint32_t sigfigs;
  uint32_t snaplen;
  uint32_t network;
} __attribute__((packed)) pcap_global_header_t;

/**
 * @brief PCAP per-packet header.
 */
typedef struct {
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint32_t incl_len;
  uint32_t orig_len;
} __attribute__((packed)) pcap_packet_header_t;

#ifdef __cplusplus
}
#endif

#endif // PCAP_SERIALIZER_H

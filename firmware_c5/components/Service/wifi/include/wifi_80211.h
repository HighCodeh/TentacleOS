// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef WIFI_80211_H
#define WIFI_80211_H

#include <stdint.h>

// 802.11 Frame Control
typedef struct {
    uint16_t protocol:2;
    uint16_t type:2;
    uint16_t subtype:4;
    uint16_t to_ds:1;
    uint16_t from_ds:1;
    uint16_t more_frag:1;
    uint16_t retry:1;
    uint16_t pwr_mgt:1;
    uint16_t more_data:1;
    uint16_t protected_frame:1;
    uint16_t order:1;
} __attribute__((packed)) wifi_frame_control_t;

// 802.11 MAC Header
typedef struct {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t addr1[6]; // Receiver / Destination
    uint8_t addr2[6]; // Transmitter / Source
    uint8_t addr3[6]; // BSSID (usually)
    uint16_t seq_ctrl;
} __attribute__((packed)) wifi_mac_header_t;

// LLC/SNAP Header for Data Frames
typedef struct {
    uint8_t dsap;
    uint8_t ssap;
    uint8_t control;
    uint8_t oui[3];
    uint16_t type; // Big Endian
} __attribute__((packed)) wifi_llc_snap_t;

#define WIFI_ETHERTYPE_EAPOL 0x888E

#endif // WIFI_80211_H
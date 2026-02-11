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

#ifndef WIFI_SNIFFER_H
#define WIFI_SNIFFER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SNIFF_TYPE_BEACON,
    SNIFF_TYPE_PROBE,
    SNIFF_TYPE_EAPOL,
    SNIFF_TYPE_PMKID,
    SNIFF_TYPE_RAW
} sniff_type_t;
bool wifi_sniffer_start(sniff_type_t type, uint8_t channel);
void wifi_sniffer_stop(void);
bool wifi_sniffer_save_to_internal_flash(const char *filename);
bool wifi_sniffer_save_to_sd_card(const char *filename);
void wifi_sniffer_free_buffer(void);
uint32_t wifi_sniffer_get_packet_count(void);
uint32_t wifi_sniffer_get_deauth_count(void);
uint32_t wifi_sniffer_get_buffer_usage(void);
void wifi_sniffer_set_snaplen(uint16_t len);
void wifi_sniffer_set_verbose(bool verbose);
bool wifi_sniffer_start_stream_sd(sniff_type_t type, uint8_t channel, const char *filename);

#endif // WIFI_SNIFFER_H

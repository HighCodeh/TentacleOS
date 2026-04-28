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

#include "mt_mod_traceroute.h"

#include <string.h>

#include "esp_log.h"

#include "meshtastic_mesh.h"

static const char *TAG = "MT_MOD_TRACEROUTE";

#define MT_TRACE_HOP_LIMIT 3
#define MT_TRACE_MAX_HOPS  8

static uint32_t s_node_num = 0;

static uint16_t enc_varint(uint8_t *buf, uint64_t value)
{
    uint16_t pos = 0;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value) byte |= 0x80;
        buf[pos++] = byte;
    } while (value);
    return pos;
}

static uint16_t enc_packed_fixed32(uint8_t *buf, uint8_t field_num,
                                     const uint32_t *values, int count)
{
    buf[0] = (field_num << 3) | 2;
    uint16_t pos = 1 + enc_varint(&buf[1], 4 * count);
    for (int i = 0; i < count; i++) {
        memcpy(&buf[pos], &values[i], 4);
        pos += 4;
    }
    return pos;
}

static uint64_t dec_varint(const uint8_t *buf, uint16_t max_len, uint16_t *out_used)
{
    uint64_t value = 0;
    uint16_t shift = 0;
    uint16_t i = 0;
    while (i < max_len && i < 10) {
        uint8_t byte = buf[i++];
        value |= ((uint64_t)(byte & 0x7F)) << shift;
        if (!(byte & 0x80)) break;
        shift += 7;
    }
    if (out_used != NULL) *out_used = i;
    return value;
}

void mt_mod_traceroute_init(uint32_t node_num)
{
    s_node_num = node_num;
    ESP_LOGI(TAG, "Initialized");
}

void mt_mod_traceroute_on_received(const mt_packet_meta_t *meta,
                                    const uint8_t *payload, uint16_t len)
{
    uint32_t route_hops[MT_TRACE_MAX_HOPS];
    int route_count = 0;

    uint16_t i = 0;
    while (i < len) {
        uint16_t consumed = 0;
        uint64_t tag = dec_varint(&payload[i], len - i, &consumed);
        if (consumed == 0) break;
        i += consumed;

        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wire_type = (uint32_t)(tag & 0x07);

        if (wire_type == 2 && field == 1) {
            uint16_t len_used = 0;
            uint32_t length = (uint32_t)dec_varint(&payload[i], len - i, &len_used);
            i += len_used;
            int n = length / 4;
            for (int k = 0; k < n && route_count < MT_TRACE_MAX_HOPS; k++) {
                memcpy(&route_hops[route_count++], &payload[i + k * 4], 4);
            }
            i += length;
        } else if (wire_type == 5) {
            if (field == 1 && route_count < MT_TRACE_MAX_HOPS) {
                memcpy(&route_hops[route_count++], &payload[i], 4);
            }
            i += 4;
        } else if (wire_type == 0) {
            uint16_t used = 0;
            dec_varint(&payload[i], len - i, &used);
            i += used;
        } else if (wire_type == 2) {
            uint16_t len_used = 0;
            uint32_t length = (uint32_t)dec_varint(&payload[i], len - i, &len_used);
            i += len_used + length;
        } else {
            break;
        }
    }

    ESP_LOGI(TAG, "TraceRoute received from 0x%08lX hops=%d target=0x%08lX",
             (unsigned long)meta->from, route_count, (unsigned long)meta->to);

    if (meta->to != s_node_num || !meta->want_response) return;

    uint32_t route[MT_TRACE_MAX_HOPS + 1];
    int n = 0;
    for (int k = 0; k < route_count && n < MT_TRACE_MAX_HOPS; k++) {
        route[n++] = route_hops[k];
    }
    route[n++] = s_node_num;

    uint8_t buf[80];
    uint16_t blen = enc_packed_fixed32(buf, 1, route, n);

    ESP_LOGI(TAG, "Replying TraceRoute -> 0x%08lX (%d hops)",
             (unsigned long)meta->from, n);
    meshtastic_mesh_send_data(meta->from, meta->channel, MT_TRACE_HOP_LIMIT,
                               MT_PORT_TRACEROUTE, buf, blen, meta->id, false, false);
}

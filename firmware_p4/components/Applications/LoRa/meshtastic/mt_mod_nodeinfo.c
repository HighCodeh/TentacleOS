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

#include "mt_mod_nodeinfo.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "meshtastic_mesh.h"
#include "meshtastic_nodedb.h"
#include "meshtastic_nvs.h"
#include "meshtastic_pki.h"
#include "meshtastic_roles.h"

static const char *TAG = "MT_MOD_NODEINFO";

#define MT_NODEINFO_BROADCAST_SECS  60
#define MT_NODEINFO_REPLY_THROTTLE  43200
#define MT_NODEINFO_HOP_LIMIT        3
#define MT_NODEINFO_HW_MODEL_PRIVATE 255
#define MT_NODEINFO_BCAST_ADDR       0xFFFFFFFF
#define MT_NODEINFO_INITIAL_MIN_S    5
#define MT_NODEINFO_INITIAL_JITTER_S 20
#define MT_NODEINFO_REQUEST_THROTTLE 60
#define MT_NODEINFO_FAST_INTERVAL_S  300
#define MT_NODEINFO_FAST_PHASE_S     1800
#define MT_NODEINFO_BOOT_BURST_COUNT 3
#define MT_NODEINFO_BOOT_BURST_MIN_S 30
#define MT_NODEINFO_BOOT_BURST_JITTER_S 20

#define MT_NODEINFO_REQUEST_SLOTS    8

typedef struct {
    uint32_t peer;
    uint32_t last_request_s;
} mt_req_slot_t;

static mt_req_slot_t s_req_slots[MT_NODEINFO_REQUEST_SLOTS];
static uint32_t s_initial_broadcast_at_s = 0;
static uint32_t s_node_num = 0;
static char     s_long_name[32] = "Highboy";
static char     s_short_name[8] = "HBX";
static uint32_t s_last_broadcast_s = 0;
static uint32_t s_last_reply_s = 0;
static uint8_t  s_broadcast_count = 0;

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

static uint16_t enc_field_varint(uint8_t *buf, uint8_t field_num, uint64_t value)
{
    buf[0] = (field_num << 3) | 0;
    return 1 + enc_varint(&buf[1], value);
}

static uint16_t enc_field_bytes(uint8_t *buf, uint8_t field_num,
                                 const uint8_t *data, uint16_t len)
{
    buf[0] = (field_num << 3) | 2;
    uint16_t pos = 1 + enc_varint(&buf[1], len);
    memcpy(&buf[pos], data, len);
    return pos + len;
}

static uint16_t build_user(uint8_t *out)
{
    uint16_t pos = 0;
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "!%08lx", (unsigned long)s_node_num);
    pos += enc_field_bytes(&out[pos], 1, (const uint8_t *)id_str,
                            (uint16_t)strlen(id_str));
    pos += enc_field_bytes(&out[pos], 2, (const uint8_t *)s_long_name,
                            (uint16_t)strlen(s_long_name));
    pos += enc_field_bytes(&out[pos], 3, (const uint8_t *)s_short_name,
                            (uint16_t)strlen(s_short_name));
    pos += enc_field_varint(&out[pos], 5, MT_NODEINFO_HW_MODEL_PRIVATE);
    pos += enc_field_varint(&out[pos], 7, 0);
    pos += enc_field_bytes(&out[pos], 8, mt_pki_get_pubkey(), 32);
    return pos;
}

void mt_mod_nodeinfo_init(uint32_t node_num)
{
    s_node_num = node_num;
    s_initial_broadcast_at_s = MT_NODEINFO_INITIAL_MIN_S +
                                (esp_random() % MT_NODEINFO_INITIAL_JITTER_S);

    char stored[32] = {0};
    int read_len = mt_nvs_get_blob("long_name", stored, sizeof(stored) - 1);
    if (read_len > 0) {
        memcpy(s_long_name, stored, read_len);
        s_long_name[read_len] = 0;
    }

    memset(stored, 0, sizeof(stored));
    read_len = mt_nvs_get_blob("short_name", stored, sizeof(stored) - 1);
    if (read_len > 0 && read_len < (int)sizeof(s_short_name)) {
        memcpy(s_short_name, stored, read_len);
        s_short_name[read_len] = 0;
    }

    ESP_LOGI(TAG, "Initialized - '%s' / '%s' (initial broadcast in %lus)",
             s_long_name, s_short_name,
             (unsigned long)s_initial_broadcast_at_s);
}

void mt_mod_nodeinfo_on_received(const mt_packet_meta_t *meta,
                                  const uint8_t *payload, uint16_t len)
{
    ESP_LOGI(TAG, "NodeInfo received from 0x%08lX (%u bytes)",
             (unsigned long)meta->from, len);

    bool was_unknown = (meta->from != 0 && meta->from != s_node_num &&
                        mt_nodedb_get(meta->from) == NULL);

    if (meta->from != 0 && meta->from != s_node_num && payload != NULL && len > 0) {
        uint8_t hops = (meta->hop_start >= meta->hop_limit)
                       ? (uint8_t)(meta->hop_start - meta->hop_limit) : 0;
        mt_nodedb_upsert_from_user(meta->from, payload, len,
                                    (float)meta->snr_db, meta->rssi_dbm, hops);
    }

    bool should_reply = meta->want_response || was_unknown;
    if (!should_reply) return;
    if (!mt_role_should_respond(mt_role_current())) {
        ESP_LOGD(TAG, "Current role does not respond to requests");
        return;
    }

    uint32_t now_s = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    if (!was_unknown && !meta->want_response && s_last_reply_s != 0 &&
        now_s - s_last_reply_s < MT_NODEINFO_REPLY_THROTTLE) {
        ESP_LOGD(TAG, "Reply throttled (last %lus ago)",
                 (unsigned long)(now_s - s_last_reply_s));
        return;
    }
    s_last_reply_s = now_s;

    uint8_t user[96];
    uint16_t ulen = build_user(user);
    ESP_LOGI(TAG, "%s NodeInfo -> 0x%08lX req_id=0x%08lX",
             was_unknown ? "First-contact reply" : "Replying",
             (unsigned long)meta->from, (unsigned long)meta->id);
    meshtastic_mesh_send_data(meta->from, meta->channel, MT_NODEINFO_HOP_LIMIT,
                               MT_PORT_NODEINFO, user, ulen, meta->id, false, false);
}

void mt_mod_nodeinfo_tick(uint32_t now_s)
{
    uint32_t interval = mt_role_nodeinfo_interval_secs(mt_role_current());
    bool no_peers = (mt_nodedb_count() == 0);
    bool fast_phase = (now_s < MT_NODEINFO_FAST_PHASE_S);

    if (no_peers && s_broadcast_count > 0 &&
        s_broadcast_count < MT_NODEINFO_BOOT_BURST_COUNT) {
        uint32_t burst = MT_NODEINFO_BOOT_BURST_MIN_S +
                         (esp_random() % MT_NODEINFO_BOOT_BURST_JITTER_S);
        if (interval == 0 || burst < interval) interval = burst;
    } else if ((no_peers || fast_phase) &&
               (interval == 0 || interval > MT_NODEINFO_FAST_INTERVAL_S)) {
        interval = MT_NODEINFO_FAST_INTERVAL_S;
    }

    if (s_last_broadcast_s == 0) {
        if (now_s < s_initial_broadcast_at_s) return;
    } else {
        if (interval == 0) return;
        if (now_s - s_last_broadcast_s < interval) return;
    }
    s_last_broadcast_s = now_s;
    s_broadcast_count++;

    uint8_t user[96];
    uint16_t ulen = build_user(user);
    ESP_LOGI(TAG, "Broadcast NodeInfo #%u: '%s' (%s)",
             s_broadcast_count, s_long_name, s_short_name);
    meshtastic_mesh_send_data(MT_NODEINFO_BCAST_ADDR, 0, MT_NODEINFO_HOP_LIMIT,
                               MT_PORT_NODEINFO, user, ulen, 0, false, false);
}

void mt_mod_nodeinfo_request(uint32_t to)
{
    if (to == 0 || to == s_node_num || to == MT_NODEINFO_BCAST_ADDR) return;

    uint32_t now_s = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    int free_slot = -1;
    int oldest_slot = 0;
    uint32_t oldest_age = 0;

    for (int i = 0; i < MT_NODEINFO_REQUEST_SLOTS; i++) {
        if (s_req_slots[i].peer == to) {
            if (now_s - s_req_slots[i].last_request_s < MT_NODEINFO_REQUEST_THROTTLE) {
                return;
            }
            s_req_slots[i].last_request_s = now_s;
            goto send;
        }
        if (s_req_slots[i].peer == 0 && free_slot < 0) free_slot = i;
        uint32_t age = now_s - s_req_slots[i].last_request_s;
        if (age > oldest_age) { oldest_age = age; oldest_slot = i; }
    }

    int slot = (free_slot >= 0) ? free_slot : oldest_slot;
    s_req_slots[slot].peer = to;
    s_req_slots[slot].last_request_s = now_s;

send:
    {
        uint8_t user[96];
        uint16_t ulen = build_user(user);
        ESP_LOGI(TAG, "NodeInfo request -> 0x%08lX (want_response)",
                 (unsigned long)to);
        meshtastic_mesh_send_data(to, 0, MT_NODEINFO_HOP_LIMIT,
                                   MT_PORT_NODEINFO, user, ulen, 0, false, true);
    }
}

void mt_mod_nodeinfo_set_name(const char *long_name, const char *short_name)
{
    if (long_name != NULL) {
        strncpy(s_long_name, long_name, sizeof(s_long_name) - 1);
        s_long_name[sizeof(s_long_name) - 1] = 0;
        mt_nvs_set_blob("long_name", s_long_name, strlen(s_long_name));
    }
    if (short_name != NULL) {
        strncpy(s_short_name, short_name, sizeof(s_short_name) - 1);
        s_short_name[sizeof(s_short_name) - 1] = 0;
        mt_nvs_set_blob("short_name", s_short_name, strlen(s_short_name));
    }
    s_last_broadcast_s = 0;
    ESP_LOGI(TAG, "Names updated - '%s' / '%s'", s_long_name, s_short_name);
}

const char *mt_mod_nodeinfo_get_long(void) { return s_long_name; }
const char *mt_mod_nodeinfo_get_short(void) { return s_short_name; }

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

#include "mt_mod_keyverify.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"

#include "meshtastic_mesh.h"
#include "meshtastic_nodedb.h"
#include "meshtastic_pki.h"

static const char *TAG = "MT_MOD_KEYVERIFY";

#define KV_STATE_IDLE                  0
#define KV_STATE_SENDER_INITIATED      1
#define KV_STATE_SENDER_AWAITING_CODE  2
#define KV_STATE_RECEIVER_AWAITING_H1  3

#define KV_MSG_INITIATE                0
#define KV_MSG_PROVIDE_SECURITY        1
#define KV_MSG_DO_VERIFY               2
#define KV_MSG_DO_NOT_VERIFY           3

#define KV_HOP_LIMIT                   3

static uint32_t s_node_num = 0;
static uint8_t  s_state = KV_STATE_IDLE;
static uint32_t s_remote_node = 0;
static uint64_t s_nonce = 0;
static uint8_t  s_hash1[32];
static uint8_t  s_hash2[32];

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

static uint16_t enc_field_varint(uint8_t *buf, uint8_t field, uint64_t v)
{
    buf[0] = (field << 3) | 0;
    return 1 + enc_varint(&buf[1], v);
}

static uint16_t enc_field_bytes(uint8_t *buf, uint8_t field,
                                 const uint8_t *data, uint16_t len)
{
    buf[0] = (field << 3) | 2;
    uint16_t pos = 1 + enc_varint(&buf[1], len);
    memcpy(&buf[pos], data, len);
    return pos + len;
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

static void reset_state(void)
{
    s_state = KV_STATE_IDLE;
    s_remote_node = 0;
    s_nonce = 0;
    memset(s_hash1, 0, sizeof(s_hash1));
    memset(s_hash2, 0, sizeof(s_hash2));
}

static void compute_hash2(uint8_t out[32], uint64_t nonce,
                            const uint8_t *remote_pub)
{
    uint8_t buf[8 + 32 + 32];
    memcpy(buf, &nonce, 8);
    memcpy(buf + 8, mt_pki_get_pubkey(), 32);
    memcpy(buf + 40, remote_pub, 32);
    mbedtls_sha256(buf, sizeof(buf), out, 0);
}

static void compute_hash1(uint8_t out[32], const uint8_t hash2[32],
                            uint32_t security_number)
{
    uint8_t buf[32 + 4];
    memcpy(buf, hash2, 32);
    memcpy(buf + 32, &security_number, 4);
    mbedtls_sha256(buf, sizeof(buf), out, 0);
}

static void send_kv_packet(uint32_t to, uint64_t nonce,
                             const uint8_t *hash1, const uint8_t *hash2)
{
    uint8_t payload[96];
    uint16_t pl = 0;
    pl += enc_field_varint(&payload[pl], 1, nonce);
    if (hash1) pl += enc_field_bytes(&payload[pl], 2, hash1, 32);
    if (hash2) pl += enc_field_bytes(&payload[pl], 3, hash2, 32);

    meshtastic_mesh_send_data(to, 0, KV_HOP_LIMIT, MT_PORT_KEY_VERIFICATION,
                               payload, pl, 0, false, false);
}

void mt_mod_keyverify_init(uint32_t node_num)
{
    s_node_num = node_num;
    reset_state();
    ESP_LOGI(TAG, "Initialized");
}

void mt_mod_keyverify_on_received(const mt_packet_meta_t *meta,
                                   const uint8_t *payload, uint16_t len)
{
    uint64_t nonce = 0;
    const uint8_t *hash1 = NULL;
    const uint8_t *hash2 = NULL;
    uint16_t hash1_len = 0, hash2_len = 0;

    uint16_t i = 0;
    while (i < len) {
        uint16_t used = 0;
        uint64_t tag = dec_varint(&payload[i], len - i, &used);
        if (used == 0) break;
        i += used;
        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wt = (uint32_t)(tag & 0x07);

        if (wt == 0) {
            uint16_t vused = 0;
            uint64_t v = dec_varint(&payload[i], len - i, &vused);
            i += vused;
            if (field == 1) nonce = v;
        } else if (wt == 2) {
            uint16_t lused = 0;
            uint32_t length = (uint32_t)dec_varint(&payload[i], len - i, &lused);
            i += lused;
            if (i + length > len) break;
            if (field == 2) { hash1 = &payload[i]; hash1_len = (uint16_t)length; }
            else if (field == 3) { hash2 = &payload[i]; hash2_len = (uint16_t)length; }
            i += length;
        } else {
            break;
        }
    }

    ESP_LOGI(TAG, "RX from 0x%08lX nonce=%llu hash1=%u hash2=%u",
             (unsigned long)meta->from, (unsigned long long)nonce,
             hash1_len, hash2_len);

    if (s_state == KV_STATE_IDLE && hash1 == NULL && hash2 == NULL) {
        const mt_node_entry_t *e = mt_nodedb_get(meta->from);
        if (e == NULL || !e->has_public_key) {
            ESP_LOGW(TAG, "Incoming INITIATE but sender pubkey unknown");
            return;
        }
        s_state = KV_STATE_RECEIVER_AWAITING_H1;
        s_remote_node = meta->from;
        s_nonce = nonce;
        compute_hash2(s_hash2, nonce, e->public_key);
        send_kv_packet(meta->from, nonce, NULL, s_hash2);
        ESP_LOGI(TAG, "Sent hash2 -> 0x%08lX, awaiting hash1",
                 (unsigned long)meta->from);
        return;
    }

    if (s_state == KV_STATE_SENDER_INITIATED && hash2_len == 32 &&
        nonce == s_nonce && meta->from == s_remote_node) {
        memcpy(s_hash2, hash2, 32);
        s_state = KV_STATE_SENDER_AWAITING_CODE;
        ESP_LOGI(TAG, "Got hash2 from 0x%08lX - waiting user to enter code",
                 (unsigned long)meta->from);
        return;
    }

    if (s_state == KV_STATE_RECEIVER_AWAITING_H1 && hash1_len == 32 &&
        nonce == s_nonce && meta->from == s_remote_node) {
        if (memcmp(hash1, s_hash1, 32) == 0) {
            ESP_LOGI(TAG, "hash1 matches - peer 0x%08lX verified",
                     (unsigned long)meta->from);
        } else {
            ESP_LOGW(TAG, "hash1 mismatch for peer 0x%08lX",
                     (unsigned long)meta->from);
        }
        reset_state();
        return;
    }

    ESP_LOGW(TAG, "Unexpected KV packet in state=%u", s_state);
}

void mt_mod_keyverify_handle_admin(uint32_t remote_nodenum,
                                    uint8_t message_type,
                                    uint64_t nonce,
                                    bool has_security,
                                    uint32_t security_number)
{
    switch (message_type) {
        case KV_MSG_INITIATE: {
            const mt_node_entry_t *e = mt_nodedb_get(remote_nodenum);
            if (e == NULL || !e->has_public_key) {
                ESP_LOGW(TAG, "INITIATE to 0x%08lX: pubkey unknown",
                         (unsigned long)remote_nodenum);
                return;
            }
            s_state = KV_STATE_SENDER_INITIATED;
            s_remote_node = remote_nodenum;
            s_nonce = (nonce != 0) ? nonce : ((uint64_t)esp_random() << 32 | esp_random());
            send_kv_packet(remote_nodenum, s_nonce, NULL, NULL);
            ESP_LOGI(TAG, "INITIATE sent to 0x%08lX nonce=%llu",
                     (unsigned long)remote_nodenum, (unsigned long long)s_nonce);
            break;
        }
        case KV_MSG_PROVIDE_SECURITY: {
            if (!has_security || s_state != KV_STATE_SENDER_AWAITING_CODE ||
                nonce != s_nonce) {
                ESP_LOGW(TAG, "PROVIDE_SECURITY invalid state/nonce");
                return;
            }
            compute_hash1(s_hash1, s_hash2, security_number);
            send_kv_packet(s_remote_node, s_nonce, s_hash1, NULL);
            ESP_LOGI(TAG, "Sent hash1 -> 0x%08lX with code=%lu",
                     (unsigned long)s_remote_node, (unsigned long)security_number);
            reset_state();
            break;
        }
        case KV_MSG_DO_VERIFY: {
            if (s_remote_node != 0) {
                ESP_LOGI(TAG, "User accepted verification for 0x%08lX",
                         (unsigned long)s_remote_node);
            }
            reset_state();
            break;
        }
        case KV_MSG_DO_NOT_VERIFY:
            ESP_LOGI(TAG, "User rejected verification");
            reset_state();
            break;
        default:
            ESP_LOGW(TAG, "Unknown KV admin message_type=%u", message_type);
            break;
    }
}

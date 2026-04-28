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

#include "mt_mod_position.h"

#include <string.h>

#include "esp_log.h"

#include "meshtastic_mesh.h"
#include "meshtastic_nvs.h"
#include "meshtastic_roles.h"

static const char *TAG = "MT_MOD_POSITION";

#define MT_POSITION_BROADCAST_SECS   900
#define MT_POSITION_HOP_LIMIT         3
#define MT_POSITION_BCAST_ADDR        0xFFFFFFFF

static uint32_t s_node_num = 0;
static bool     s_has_fixed_position = false;
static int32_t  s_lat_e7 = 0;
static int32_t  s_lon_e7 = 0;
static int32_t  s_alt_m  = 0;
static uint32_t s_last_broadcast_s = 0;

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

static uint16_t enc_field_varint(uint8_t *buf, uint8_t field_num, int64_t value)
{
    buf[0] = (field_num << 3) | 0;
    return 1 + enc_varint(&buf[1], (uint64_t)value);
}

static uint16_t enc_field_fixed32(uint8_t *buf, uint8_t field_num, uint32_t value)
{
    buf[0] = (field_num << 3) | 5;
    memcpy(&buf[1], &value, 4);
    return 5;
}

static uint16_t build_position(uint8_t *out)
{
    uint16_t pos = 0;
    pos += enc_field_fixed32(&out[pos], 1, (uint32_t)s_lat_e7);
    pos += enc_field_fixed32(&out[pos], 2, (uint32_t)s_lon_e7);
    pos += enc_field_varint(&out[pos], 3, s_alt_m);
    return pos;
}

void mt_mod_position_init(uint32_t node_num)
{
    s_node_num = node_num;

    int32_t stored[3] = {0};
    int read_len = mt_nvs_get_blob("fix_pos", stored, sizeof(stored));
    if (read_len == sizeof(stored)) {
        s_lat_e7 = stored[0];
        s_lon_e7 = stored[1];
        s_alt_m  = stored[2];
        s_has_fixed_position = true;
        ESP_LOGI(TAG, "Fixed position loaded - lat=%.6f lon=%.6f alt=%ldm",
                 s_lat_e7 / 1.0e7, s_lon_e7 / 1.0e7, (long)s_alt_m);
    } else {
        ESP_LOGI(TAG, "No fixed position configured");
    }
}

void mt_mod_position_on_received(const mt_packet_meta_t *meta,
                                  const uint8_t *payload, uint16_t len)
{
    (void)payload;
    ESP_LOGI(TAG, "Position received from 0x%08lX (%u bytes)",
             (unsigned long)meta->from, len);

    if (!meta->want_response || !s_has_fixed_position) return;
    if (!mt_role_should_respond(mt_role_current())) return;

    uint8_t pos[48];
    uint16_t plen = build_position(pos);
    meshtastic_mesh_send_data(meta->from, meta->channel, MT_POSITION_HOP_LIMIT,
                               MT_PORT_POSITION, pos, plen, meta->id, false, false);
}

void mt_mod_position_tick(uint32_t now_s)
{
    if (!s_has_fixed_position) return;
    uint32_t interval = mt_role_position_interval_secs(mt_role_current());
    if (interval == 0) return;
    if (now_s - s_last_broadcast_s < interval) return;
    s_last_broadcast_s = now_s;

    uint8_t pos[48];
    uint16_t plen = build_position(pos);
    ESP_LOGI(TAG, "Broadcast Position - lat=%.6f lon=%.6f",
             s_lat_e7 / 1.0e7, s_lon_e7 / 1.0e7);
    meshtastic_mesh_send_data(MT_POSITION_BCAST_ADDR, 0, MT_POSITION_HOP_LIMIT,
                               MT_PORT_POSITION, pos, plen, 0, false, false);
}

void mt_mod_position_set_fixed(int32_t lat_e7, int32_t lon_e7, int32_t alt_m)
{
    s_lat_e7 = lat_e7;
    s_lon_e7 = lon_e7;
    s_alt_m  = alt_m;
    s_has_fixed_position = true;

    int32_t stored[3] = { lat_e7, lon_e7, alt_m };
    mt_nvs_set_blob("fix_pos", stored, sizeof(stored));

    s_last_broadcast_s = 0;
    ESP_LOGI(TAG, "Fixed position set - lat=%.6f lon=%.6f alt=%ldm",
             lat_e7 / 1.0e7, lon_e7 / 1.0e7, (long)alt_m);
}

void mt_mod_position_remove_fixed(void)
{
    s_has_fixed_position = false;
    mt_nvs_erase("fix_pos");
    ESP_LOGI(TAG, "Fixed position removed");
}

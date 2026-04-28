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

#include "mt_mod_telemetry.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "meshtastic_mesh.h"
#include "meshtastic_roles.h"

static const char *TAG = "MT_MOD_TELEMETRY";

#define MT_TELEM_INTERVAL_SECS   60
#define MT_TELEM_HOP_LIMIT       3
#define MT_TELEM_BCAST_ADDR      0xFFFFFFFF
#define MT_TELEM_BATTERY_USB_PWR 101

static uint32_t s_node_num = 0;
static uint32_t s_last_broadcast_s = 0;
static uint32_t s_uptime_wrap_count = 0;
static uint32_t s_uptime_last_ms = 0;

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

static uint32_t compute_uptime_seconds(void)
{
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (now_ms < s_uptime_last_ms) {
        s_uptime_wrap_count++;
    }
    s_uptime_last_ms = now_ms;
    uint64_t total_ms = ((uint64_t)s_uptime_wrap_count << 32) | now_ms;
    return (uint32_t)(total_ms / 1000);
}

static uint16_t build_device_metrics(uint8_t *out)
{
    uint16_t pos = 0;
    pos += enc_field_varint(&out[pos], 1, MT_TELEM_BATTERY_USB_PWR);
    pos += enc_field_varint(&out[pos], 5, compute_uptime_seconds());
    return pos;
}

static uint16_t build_telemetry_frame(uint8_t *out, uint32_t unix_time)
{
    uint16_t pos = 0;
    out[pos++] = (1 << 3) | 5;
    memcpy(&out[pos], &unix_time, 4);
    pos += 4;

    uint8_t device_metrics[32];
    uint16_t dm_len = build_device_metrics(device_metrics);
    pos += enc_field_bytes(&out[pos], 2, device_metrics, dm_len);
    return pos;
}

void mt_mod_telemetry_init(uint32_t node_num)
{
    s_node_num = node_num;
    ESP_LOGI(TAG, "Initialized - broadcast every %ds", MT_TELEM_INTERVAL_SECS);
}

void mt_mod_telemetry_on_received(const mt_packet_meta_t *meta,
                                   const uint8_t *payload, uint16_t len)
{
    (void)payload;
    ESP_LOGI(TAG, "Telemetry received from 0x%08lX (%u bytes)",
             (unsigned long)meta->from, len);

    if (!meta->want_response) return;
    if (!mt_role_should_respond(mt_role_current())) return;

    uint8_t frame[64];
    uint16_t flen = build_telemetry_frame(frame, compute_uptime_seconds());
    meshtastic_mesh_send_data(meta->from, meta->channel, MT_TELEM_HOP_LIMIT,
                               MT_PORT_TELEMETRY, frame, flen, meta->id, false, false);
}

void mt_mod_telemetry_tick(uint32_t now_s)
{
    uint32_t interval = mt_role_telemetry_interval_secs(mt_role_current());
    if (interval == 0) return;
    if (now_s - s_last_broadcast_s < interval) return;
    s_last_broadcast_s = now_s;

    uint8_t frame[64];
    uint16_t flen = build_telemetry_frame(frame, now_s);
    ESP_LOGI(TAG, "Broadcast Telemetry - uptime=%lus heap=%lu",
             (unsigned long)compute_uptime_seconds(),
             (unsigned long)esp_get_free_heap_size());
    meshtastic_mesh_send_data(MT_TELEM_BCAST_ADDR, 0, MT_TELEM_HOP_LIMIT,
                               MT_PORT_TELEMETRY, frame, flen, 0, false, false);
}

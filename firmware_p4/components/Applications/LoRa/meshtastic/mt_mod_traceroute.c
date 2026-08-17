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

#include "mt_mod_traceroute.h"

#include <string.h>

#include "esp_log.h"

#include "meshtastic_mesh.h"

static const char *TAG = "MT_MOD_TRACEROUTE";

#define MT_TRACE_HOP_LIMIT 3

static uint32_t s_node_num = 0;
static bool s_pending = false;
static uint32_t s_pending_target = 0;
static bool s_result_ready = false;
static uint32_t s_result_from = 0;
static uint32_t s_result_hops[MT_TRACE_MAX_HOPS];
static int s_result_count = 0;

static uint16_t enc_varint(uint8_t *buf, uint64_t value) {
  uint16_t pos = 0;
  do {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value)
      byte |= 0x80;
    buf[pos++] = byte;
  } while (value);
  return pos;
}

static uint16_t
enc_packed_fixed32(uint8_t *buf, uint8_t field_num, const uint32_t *values, int count) {
  buf[0] = (field_num << 3) | 2;
  uint16_t pos = 1 + enc_varint(&buf[1], 4 * count);
  for (int i = 0; i < count; i++) {
    memcpy(&buf[pos], &values[i], 4);
    pos += 4;
  }
  return pos;
}

static uint64_t dec_varint(const uint8_t *buf, uint16_t max_len, uint16_t *out_used) {
  uint64_t value = 0;
  uint16_t shift = 0;
  uint16_t i = 0;
  while (i < max_len && i < 10) {
    uint8_t byte = buf[i++];
    value |= ((uint64_t)(byte & 0x7F)) << shift;
    if (!(byte & 0x80))
      break;
    shift += 7;
  }
  if (out_used != NULL)
    *out_used = i;
  return value;
}

void mt_mod_traceroute_init(uint32_t node_num) {
  s_node_num = node_num;
  ESP_LOGI(TAG, "Initialized");
}

void mt_mod_traceroute_on_received(const mt_packet_meta_t *meta,
                                   const uint8_t *payload,
                                   uint16_t len) {
  uint32_t route_hops[MT_TRACE_MAX_HOPS];
  int route_count = 0;

  uint16_t i = 0;
  while (i < len) {
    uint16_t consumed = 0;
    uint64_t tag = dec_varint(&payload[i], len - i, &consumed);
    if (consumed == 0)
      break;
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

  ESP_LOGI(TAG,
           "TraceRoute received from 0x%08lX hops=%d target=0x%08lX",
           (unsigned long)meta->from,
           route_count,
           (unsigned long)meta->to);

  if (s_pending && meta->to == s_node_num && !meta->want_response &&
      meta->from == s_pending_target) {
    s_result_count = 0;
    for (int k = 0; k < route_count && s_result_count < MT_TRACE_MAX_HOPS; k++)
      s_result_hops[s_result_count++] = route_hops[k];
    s_result_from = meta->from;
    s_result_ready = true;
    s_pending = false;
    ESP_LOGI(
        TAG, "TraceRoute result from 0x%08lX (%d hops)", (unsigned long)meta->from, s_result_count);
    return;
  }

  if (meta->to != s_node_num || !meta->want_response)
    return;

  uint32_t route[MT_TRACE_MAX_HOPS + 1];
  int n = 0;
  for (int k = 0; k < route_count && n < MT_TRACE_MAX_HOPS; k++) {
    route[n++] = route_hops[k];
  }
  route[n++] = s_node_num;

  uint8_t buf[80];
  uint16_t blen = enc_packed_fixed32(buf, 1, route, n);

  ESP_LOGI(TAG, "Replying TraceRoute -> 0x%08lX (%d hops)", (unsigned long)meta->from, n);
  meshtastic_mesh_send_data(meta->from,
                            meta->channel,
                            MT_TRACE_HOP_LIMIT,
                            MT_PORT_TRACEROUTE,
                            buf,
                            blen,
                            meta->id,
                            false,
                            false);
}

void mt_mod_traceroute_start(uint32_t to) {
  uint8_t req[1] = {0};
  s_pending = true;
  s_pending_target = to;
  s_result_ready = false;
  ESP_LOGI(TAG, "TraceRoute -> 0x%08lX", (unsigned long)to);
  meshtastic_mesh_send_data(to, 0, MT_TRACE_HOP_LIMIT, MT_PORT_TRACEROUTE, req, 0, 0, false, true);
}

bool mt_mod_traceroute_is_pending(void) {
  return s_pending;
}

bool mt_mod_traceroute_get_result(uint32_t *out_hops, int *out_count, uint32_t *out_target) {
  if (!s_result_ready)
    return false;
  if (out_hops != NULL) {
    for (int k = 0; k < s_result_count; k++)
      out_hops[k] = s_result_hops[k];
  }
  if (out_count != NULL)
    *out_count = s_result_count;
  if (out_target != NULL)
    *out_target = s_result_from;
  return true;
}

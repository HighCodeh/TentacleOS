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

#include "meshtastic_mesh.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/aes.h"

#include "sx1262.h"
#include "sx1262_regs.h"

#ifndef MESH_HEADLESS
#define MESH_HEADLESS 1
#endif

#if !MESH_HEADLESS
#include "meshtastic_phoneapi.h"
#endif

#include "mt_modules.h"
#include "meshtastic_channels.h"
#include "meshtastic_pkt_history.h"
#include "meshtastic_roles.h"

static const char *TAG = "MESH_RADIO";

#define MT_HEADER_SIZE     16
#define MT_MAX_PAYLOAD     (255 - MT_HEADER_SIZE)
#define MT_HOP_LIMIT_MASK  0x07
#define MT_WANT_ACK_MASK   0x08
#define MT_VIA_MQTT_MASK   0x10
#define MT_HOP_START_MASK  0xE0
#define MT_HOP_START_SHIFT 5

typedef struct __attribute__((packed)) {
  uint32_t to;
  uint32_t from;
  uint32_t id;
  uint8_t flags;
  uint8_t channel;
  uint8_t next_hop;
  uint8_t relay_node;
} mesh_packet_header_t;

#define MESH_TX_QUEUE_SIZE 8

#define MESH_PRIO_ACK        120
#define MESH_PRIO_ALERT      110
#define MESH_PRIO_HIGH       100
#define MESH_PRIO_RESPONSE   80
#define MESH_PRIO_RELIABLE   70
#define MESH_PRIO_DEFAULT    64
#define MESH_PRIO_BACKGROUND 10
#define MESH_PRIO_MIN        1

typedef struct {
  bool valid;
  uint8_t priority;
  uint32_t scheduled_ms;
  uint16_t len;
  uint8_t buf[256];
} mesh_tx_entry_t;

static mesh_tx_entry_t s_tx_queue[MESH_TX_QUEUE_SIZE];
static SemaphoreHandle_t s_tx_mutex = NULL;

static uint32_t s_node_num = 0;
static volatile bool s_is_running = false;
static TaskHandle_t s_mesh_task_handle = NULL;

#define CAD_NOTIFY_BUSY        (1U << 0)
#define CAD_NOTIFY_CLEAR       (1U << 1)
#define CSMA_MAX_RETRIES       4
#define CSMA_BACKOFF_BASE_MS   50
#define CSMA_BACKOFF_JITTER_MS 100
#define CAD_TIMEOUT_MS         100

static void on_rx_done(const sx1262_packet_t *pkt, void *ctx);
static void on_tx_done(void *ctx);
static void on_timeout(void *ctx);
static void mesh_task(void *arg);
static void mesh_crypt(uint32_t from_node, uint32_t packet_id, uint8_t *data, size_t len);
static void dedup_add(uint32_t from, uint32_t id);
static void process_rx_packet(const sx1262_packet_t *pkt);
static bool tx_queue_push(const uint8_t *buf, uint16_t len, uint8_t priority, uint32_t delay_ms);
static int tx_queue_pop_highest(mesh_tx_entry_t *out, uint32_t now_ms);

static uint16_t enc_varint(uint8_t *buf, uint64_t val) {
  uint16_t pos = 0;
  do {
    uint8_t b = val & 0x7F;
    val >>= 7;
    if (val)
      b |= 0x80;
    buf[pos++] = b;
  } while (val);
  return pos;
}

static uint16_t enc_field_varint(uint8_t *buf, uint8_t fn, uint64_t val) {
  uint16_t pos = 0;
  buf[pos++] = (fn << 3) | 0;
  pos += enc_varint(&buf[pos], val);
  return pos;
}

static uint16_t enc_field_fixed32(uint8_t *buf, uint8_t fn, uint32_t val) {
  uint16_t pos = 0;
  buf[pos++] = (fn << 3) | 5;
  memcpy(&buf[pos], &val, 4);
  return pos + 4;
}

static uint16_t enc_field_bytes(uint8_t *buf, uint8_t fn, const uint8_t *data, uint16_t len) {
  uint16_t pos = 0;
  buf[pos++] = (fn << 3) | 2;
  pos += enc_varint(&buf[pos], len);
  memcpy(&buf[pos], data, len);
  return pos + len;
}

static void mesh_crypt_with_psk(
    const uint8_t *psk, uint32_t from_node, uint32_t packet_id, uint8_t *data, size_t len) {
  if (!psk)
    psk = mt_channel_primary_psk();

  uint8_t nonce[16];
  memset(nonce, 0, sizeof(nonce));
  uint64_t pid64 = packet_id;
  memcpy(nonce, &pid64, 8);
  memcpy(nonce + 8, &from_node, 4);

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, psk, 128);

  uint8_t stream_block[16];
  size_t nc_off = 0;
  uint8_t nonce_counter[16];
  memcpy(nonce_counter, nonce, 16);
  memset(stream_block, 0, 16);

  uint8_t output[256];
  mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce_counter, stream_block, data, output);
  memcpy(data, output, len);

  mbedtls_aes_free(&ctx);
}

static void mesh_crypt(uint32_t from_node, uint32_t packet_id, uint8_t *data, size_t len) {
  mesh_crypt_with_psk(NULL, from_node, packet_id, data, len);
}

static void dedup_add(uint32_t from, uint32_t id) {
  mt_pkt_history_check_add(from, id, 3, 0, NULL);
}

static bool tx_queue_push(const uint8_t *buf, uint16_t len, uint8_t priority, uint32_t delay_ms) {
  if (!buf || len == 0 || len > 256)
    return false;
  if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }

  int slot = -1;

  for (int i = 0; i < MESH_TX_QUEUE_SIZE; i++) {
    if (!s_tx_queue[i].valid) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    int lowest_idx = 0;
    uint8_t lowest_prio = s_tx_queue[0].priority;
    for (int i = 1; i < MESH_TX_QUEUE_SIZE; i++) {
      if (s_tx_queue[i].priority < lowest_prio) {
        lowest_prio = s_tx_queue[i].priority;
        lowest_idx = i;
      }
    }
    if (priority > lowest_prio) {
      slot = lowest_idx;
      ESP_LOGW(TAG, "TX queue full, evictando prio=%d", lowest_prio);
    }
  }

  if (slot < 0) {
    xSemaphoreGive(s_tx_mutex);
    ESP_LOGW(TAG, "TX queue cheia, drop prio=%d len=%d", priority, len);
    return false;
  }

  uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
  s_tx_queue[slot].priority = priority;
  s_tx_queue[slot].scheduled_ms = now + delay_ms;
  s_tx_queue[slot].len = len;
  memcpy(s_tx_queue[slot].buf, buf, len);
  s_tx_queue[slot].valid = true;

  xSemaphoreGive(s_tx_mutex);
  return true;
}

static int tx_queue_pop_highest(mesh_tx_entry_t *out, uint32_t now_ms) {
  if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
    return 0;

  int best = -1;
  uint8_t best_prio = 0;
  for (int i = 0; i < MESH_TX_QUEUE_SIZE; i++) {
    if (!s_tx_queue[i].valid)
      continue;
    if (s_tx_queue[i].scheduled_ms > now_ms)
      continue;
    if (best < 0 || s_tx_queue[i].priority > best_prio) {
      best = i;
      best_prio = s_tx_queue[i].priority;
    }
  }

  if (best < 0) {
    xSemaphoreGive(s_tx_mutex);
    return 0;
  }

  memcpy(out, &s_tx_queue[best], sizeof(mesh_tx_entry_t));
  s_tx_queue[best].valid = false;
  xSemaphoreGive(s_tx_mutex);
  return 1;
}

static void process_rx_packet(const sx1262_packet_t *pkt) {
  if (pkt->len < MT_HEADER_SIZE) {
    ESP_LOGW(TAG, "RX: pacote curto (%d bytes)", pkt->len);
    return;
  }

  mesh_packet_header_t hdr;
  memcpy(&hdr, pkt->buf, sizeof(hdr));

  uint8_t hop_limit = hdr.flags & MT_HOP_LIMIT_MASK;
  uint8_t hop_start = (hdr.flags & MT_HOP_START_MASK) >> MT_HOP_START_SHIFT;
  bool want_ack = !!(hdr.flags & MT_WANT_ACK_MASK);
  uint16_t payload_len = pkt->len - MT_HEADER_SIZE;

  ESP_LOGI(TAG,
           "RX: from=0x%08lX to=0x%08lX id=0x%08lX ch=%d hop=%d/%d len=%d RSSI=%d SNR=%d",
           (unsigned long)hdr.from,
           (unsigned long)hdr.to,
           (unsigned long)hdr.id,
           hdr.channel,
           hop_limit,
           hop_start,
           payload_len,
           pkt->rssi_pkt_dbm,
           pkt->snr_pkt_db);

  if (hdr.from == s_node_num) {
    ESP_LOGD(TAG, "RX: pacote proprio, skip");
    return;
  }

  bool was_upgraded = false;
  bool seen_before =
      mt_pkt_history_check_add(hdr.from, hdr.id, hop_limit, hdr.relay_node, &was_upgraded);

  if (seen_before && !was_upgraded) {
    mt_role_t role = mt_role_current();
    if (mt_role_cancels_on_duplicate(role)) {
      ESP_LOGD(TAG, "RX duplicata (ja retransmitiu) — cancelando rebroadcast");
    } else {
      ESP_LOGD(TAG, "RX duplicata (role=%s nao cancela)", mt_role_name(role));
    }
    return;
  }

  if (was_upgraded) {
    ESP_LOGD(TAG, "RX upgrade: hop %u melhor do que visto antes — reprocessa", hop_limit);
  }

  const uint8_t *psk = NULL;
  uint8_t ch_idx = 0;
  if (!mt_channel_lookup_by_hash(hdr.channel, &ch_idx, &psk)) {
    psk = mt_channel_primary_psk();
  }

  uint8_t decrypted[MT_MAX_PAYLOAD];
  memcpy(decrypted, pkt->buf + MT_HEADER_SIZE, payload_len);
  mesh_crypt_with_psk(psk, hdr.from, hdr.id, decrypted, payload_len);

  mt_packet_meta_t meta = {
      .from = hdr.from,
      .to = hdr.to,
      .id = hdr.id,
      .channel = hdr.channel,
      .hop_limit = hop_limit,
      .hop_start = hop_start,
      .rssi_dbm = pkt->rssi_pkt_dbm,
      .snr_db = pkt->snr_pkt_db,
      .want_ack = want_ack,
      .want_response = false,
      .request_id = 0,
  };
  mt_modules_dispatch(&meta, decrypted, payload_len);

#if !MESH_HEADLESS
  {
    uint8_t mp[320];
    uint16_t mp_len = 0;
    mp_len += enc_field_fixed32(&mp[mp_len], 1, hdr.from);
    mp_len += enc_field_fixed32(&mp[mp_len], 2, hdr.to);
    mp_len += enc_field_varint(&mp[mp_len], 3, hdr.channel);
    mp_len += enc_field_bytes(&mp[mp_len], 4, decrypted, payload_len);
    mp_len += enc_field_fixed32(&mp[mp_len], 6, hdr.id);

    uint32_t now_unix = 1776192000UL + (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    mp_len += enc_field_fixed32(&mp[mp_len], 7, now_unix);

    float snr_f = (float)pkt->snr_pkt_db;
    mp[mp_len++] = (8 << 3) | 5;
    memcpy(&mp[mp_len], &snr_f, 4);
    mp_len += 4;

    mp_len += enc_field_varint(&mp[mp_len], 9, hop_limit);
    mp_len += enc_field_varint(&mp[mp_len], 10, want_ack ? 1 : 0);
    mp_len += enc_field_varint(&mp[mp_len], 12, (uint64_t)(int64_t)pkt->rssi_pkt_dbm);
    mp_len += enc_field_varint(&mp[mp_len], 15, hop_start);

    phoneapi_push_packet(mp, mp_len);
    ESP_LOGI(TAG, "RX -> phoneapi: %d bytes MeshPacket", mp_len);
  }
#else
  ESP_LOGI(
      TAG, "RX (headless): from=0x%08lX, %d bytes decoded", (unsigned long)hdr.from, payload_len);
#endif

  mt_role_t my_role = mt_role_current();
  mt_rebr_mode_t my_mode = mt_rebr_mode_current();

  uint32_t portnum_hint = 0;
  if (payload_len >= 2 && decrypted[0] == 0x08) {
    uint16_t vb = 0;
    uint64_t v = 0, shift = 0;
    for (int k = 1; k < 5 && k < payload_len; k++) {
      v |= ((uint64_t)(decrypted[k] & 0x7F)) << shift;
      vb++;
      if (!(decrypted[k] & 0x80))
        break;
      shift += 7;
    }
    (void)vb;
    portnum_hint = (uint32_t)v;
  }

  bool is_rebroadcaster = mt_role_is_rebroadcaster(my_role);
  bool mode_allows = mt_rebr_should_forward(my_mode, portnum_hint, true);

  if (hdr.to == 0xFFFFFFFF && hop_limit > 0 && is_rebroadcaster && mode_allows) {
    uint8_t new_flags = (hdr.flags & ~MT_HOP_LIMIT_MASK) | ((hop_limit - 1) & MT_HOP_LIMIT_MASK);

    uint8_t rebroadcast[256];
    mesh_packet_header_t new_hdr = hdr;
    new_hdr.flags = new_flags;
    new_hdr.relay_node = (uint8_t)(s_node_num & 0xFF);
    memcpy(rebroadcast, &new_hdr, MT_HEADER_SIZE);

    uint8_t re_payload[MT_MAX_PAYLOAD];
    memcpy(re_payload, decrypted, payload_len);
    mesh_crypt_with_psk(psk, hdr.from, hdr.id, re_payload, payload_len);
    memcpy(rebroadcast + MT_HEADER_SIZE, re_payload, payload_len);

    uint32_t backoff;
    if (mt_role_is_late_rebroadcaster(my_role)) {
      backoff = 300 + (esp_random() % 1200);
    } else {
      backoff = 20 + (esp_random() % 180);
    }

    uint16_t total = MT_HEADER_SIZE + payload_len;
    if (tx_queue_push(rebroadcast, total, MESH_PRIO_DEFAULT, backoff)) {
      ESP_LOGI(TAG,
               "Rebroadcast enfileirado: hop %d->%d, delay %lu ms",
               hop_limit,
               hop_limit - 1,
               (unsigned long)backoff);
    }
  }
}

static uint32_t s_tx_pkt_id = 0;

static uint16_t build_wire_packet(uint8_t *out,
                                  uint32_t to,
                                  uint8_t channel,
                                  uint8_t hop_limit,
                                  const uint8_t *payload,
                                  uint16_t payload_len,
                                  uint32_t *out_pkt_id) {
  if (payload_len > MT_MAX_PAYLOAD)
    return 0;

  if (s_tx_pkt_id == 0)
    s_tx_pkt_id = esp_random();
  uint32_t pkt_id = s_tx_pkt_id++;
  if (out_pkt_id)
    *out_pkt_id = pkt_id;

  mesh_packet_header_t hdr = {0};
  hdr.from = s_node_num;
  hdr.to = to;
  hdr.id = pkt_id;
  hdr.flags =
      (hop_limit & MT_HOP_LIMIT_MASK) | ((hop_limit << MT_HOP_START_SHIFT) & MT_HOP_START_MASK);
  hdr.channel = channel;
  hdr.relay_node = (uint8_t)(s_node_num & 0xFF);

  uint8_t encrypted[MT_MAX_PAYLOAD];
  memcpy(encrypted, payload, payload_len);
  mesh_crypt(s_node_num, pkt_id, encrypted, payload_len);

  memcpy(out, &hdr, MT_HEADER_SIZE);
  memcpy(out + MT_HEADER_SIZE, encrypted, payload_len);
  return MT_HEADER_SIZE + payload_len;
}

esp_err_t meshtastic_mesh_send_nodeinfo(void) {
  uint8_t user[64];
  uint16_t u_len = 0;

  char id_str[16];
  snprintf(id_str, sizeof(id_str), "!%08lx", (unsigned long)s_node_num);
  u_len += enc_field_bytes(&user[u_len], 1, (const uint8_t *)id_str, (uint16_t)strlen(id_str));

#if MESH_HEADLESS
  const char *long_name = "Highboy P4";
  const char *short_name = "HBP4";
#else
  const char *long_name = "Highboy S3";
  const char *short_name = "HBS3";
#endif
  u_len +=
      enc_field_bytes(&user[u_len], 2, (const uint8_t *)long_name, (uint16_t)strlen(long_name));
  u_len +=
      enc_field_bytes(&user[u_len], 3, (const uint8_t *)short_name, (uint16_t)strlen(short_name));
  u_len += enc_field_varint(&user[u_len], 5, 255);

  uint8_t data[96];
  uint16_t d_len = 0;
  d_len += enc_field_varint(&data[d_len], 1, 4);
  d_len += enc_field_bytes(&data[d_len], 2, user, u_len);

  uint8_t wire[256];
  uint32_t pkt_id;
  uint16_t wire_len = build_wire_packet(wire, 0xFFFFFFFF, 0, 3, data, d_len, &pkt_id);
  if (wire_len == 0)
    return ESP_ERR_INVALID_ARG;

  dedup_add(s_node_num, pkt_id);

  ESP_LOGI(TAG,
           "NodeInfo enfileirado: %s (%s) id=0x%08lX",
           long_name,
           short_name,
           (unsigned long)pkt_id);
  return tx_queue_push(wire, wire_len, MESH_PRIO_DEFAULT, 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t meshtastic_mesh_send_text(const char *text, uint32_t to) {
  uint16_t text_len = (uint16_t)strlen(text);
  if (text_len > 200)
    text_len = 200;

  uint8_t data[220];
  uint16_t d_len = 0;
  d_len += enc_field_varint(&data[d_len], 1, 1);
  d_len += enc_field_bytes(&data[d_len], 2, (const uint8_t *)text, text_len);

  uint8_t wire[256];
  uint32_t pkt_id;
  uint16_t wire_len = build_wire_packet(wire, to, 0, 3, data, d_len, &pkt_id);
  if (wire_len == 0)
    return ESP_ERR_INVALID_ARG;

  dedup_add(s_node_num, pkt_id);

  ESP_LOGI(TAG,
           "Text enfileirado: \"%s\" -> 0x%08lX id=0x%08lX",
           text,
           (unsigned long)to,
           (unsigned long)pkt_id);
  return tx_queue_push(wire, wire_len, MESH_PRIO_HIGH, 0) ? ESP_OK : ESP_FAIL;
}

static uint64_t dec_varint(const uint8_t *buf, uint16_t maxlen, uint16_t *consumed) {
  uint64_t val = 0;
  uint16_t shift = 0;
  uint16_t i = 0;
  while (i < maxlen && i < 10) {
    uint8_t b = buf[i++];
    val |= ((uint64_t)(b & 0x7F)) << shift;
    if (!(b & 0x80))
      break;
    shift += 7;
  }
  if (consumed)
    *consumed = i;
  return val;
}

static inline uint32_t rd_fixed32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t priority_for_portnum(uint32_t portnum) {
  switch (portnum) {
    case 5:
      return MESH_PRIO_ACK;
    case 11:
      return MESH_PRIO_ALERT;
    case 6:
      return MESH_PRIO_HIGH;
    case 1:
      return MESH_PRIO_HIGH;
    case 7:
      return MESH_PRIO_HIGH;
    case 4:
      return MESH_PRIO_DEFAULT;
    case 3:
      return MESH_PRIO_BACKGROUND;
    case 67:
      return MESH_PRIO_BACKGROUND;
    case 70:
      return MESH_PRIO_DEFAULT;
    case 71:
      return MESH_PRIO_BACKGROUND;
    default:
      return MESH_PRIO_DEFAULT;
  }
}

esp_err_t meshtastic_mesh_send_data(uint32_t to,
                                    uint8_t channel,
                                    uint8_t hop_limit,
                                    uint8_t portnum,
                                    const uint8_t *payload,
                                    uint16_t plen,
                                    uint32_t request_id,
                                    bool want_ack) {
  if (plen > 200)
    return ESP_ERR_INVALID_SIZE;

  uint8_t data[220];
  uint16_t d_len = 0;
  d_len += enc_field_varint(&data[d_len], 1, portnum);
  d_len += enc_field_bytes(&data[d_len], 2, payload, plen);
  if (request_id != 0) {
    d_len += enc_field_varint(&data[d_len], 3, request_id);
  }
  if (want_ack) {
    d_len += enc_field_varint(&data[d_len], 4, 1);
  }

  uint8_t wire[256];
  uint32_t pkt_id;
  uint16_t wire_len = build_wire_packet(wire, to, channel, hop_limit, data, d_len, &pkt_id);
  if (wire_len == 0)
    return ESP_ERR_INVALID_ARG;

  if (want_ack) {
    wire[12] |= MT_WANT_ACK_MASK;
  }

  dedup_add(s_node_num, pkt_id);

  uint8_t prio;
  switch (portnum) {
    case 5:
      prio = MESH_PRIO_ACK;
      break;
    case 11:
      prio = MESH_PRIO_ALERT;
      break;
    case 6:
    case 1:
    case 7:
      prio = MESH_PRIO_HIGH;
      break;
    case 4:
      prio = MESH_PRIO_DEFAULT;
      break;
    default:
      prio = MESH_PRIO_BACKGROUND;
  }

  ESP_LOGI(TAG,
           "send_data: port=%u to=0x%08lX ch=%u prio=%u len=%u req=0x%08lX",
           portnum,
           (unsigned long)to,
           channel,
           prio,
           wire_len,
           (unsigned long)request_id);
  return tx_queue_push(wire, wire_len, prio, 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t meshtastic_mesh_send(const uint8_t *mp_data, uint16_t mp_len) {
  if (!mp_data || mp_len == 0)
    return ESP_ERR_INVALID_ARG;

  uint32_t to = 0xFFFFFFFF;
  uint32_t from = s_node_num;
  uint32_t pkt_id = 0;
  uint8_t channel = 0;
  uint8_t hop_limit = 3;
  bool want_ack = false;
  const uint8_t *decoded_ptr = NULL;
  uint16_t decoded_len = 0;
  const uint8_t *encrypted_ptr = NULL;
  uint16_t encrypted_len = 0;
  uint32_t portnum_hint = 0;

  uint16_t i = 0;
  while (i < mp_len) {
    uint16_t vb = 0;
    uint64_t tag = dec_varint(&mp_data[i], mp_len - i, &vb);
    if (vb == 0)
      break;
    i += vb;

    uint32_t field = (uint32_t)(tag >> 3);
    uint32_t wt = (uint32_t)(tag & 0x07);

    if (wt == 0) {
      uint16_t cb = 0;
      uint64_t v = dec_varint(&mp_data[i], mp_len - i, &cb);
      i += cb;
      switch (field) {
        case 3:
          channel = (uint8_t)v;
          break;
        case 9:
          hop_limit = (uint8_t)(v & 0x07);
          break;
        case 10:
          want_ack = !!v;
          break;
        default:
          break;
      }
    } else if (wt == 2) {
      uint16_t lb = 0;
      uint32_t L = (uint32_t)dec_varint(&mp_data[i], mp_len - i, &lb);
      i += lb;
      if (i + L > mp_len) {
        break;
      }
      if (field == 4) {
        decoded_ptr = &mp_data[i];
        decoded_len = (uint16_t)L;

        if (decoded_len >= 2 && decoded_ptr[0] == 0x08) {
          uint16_t pvb = 0;
          portnum_hint = (uint32_t)dec_varint(&decoded_ptr[1], decoded_len - 1, &pvb);
        }
      } else if (field == 8) {
        encrypted_ptr = &mp_data[i];
        encrypted_len = (uint16_t)L;
      }
      i += L;
    } else if (wt == 5) {
      if (i + 4 > mp_len)
        break;
      uint32_t v = rd_fixed32(&mp_data[i]);
      i += 4;
      switch (field) {
        case 1:
          from = v;
          break;
        case 2:
          to = v;
          break;
        case 6:
          pkt_id = v;
          break;
        default:
          break;
      }
    } else {
      break;
    }
  }

  if (pkt_id == 0) {
    if (s_tx_pkt_id == 0)
      s_tx_pkt_id = esp_random();
    pkt_id = s_tx_pkt_id++;
  }

  from = s_node_num;

  const uint8_t *payload_src = NULL;
  uint16_t payload_len = 0;
  bool needs_encrypt = false;

  if (decoded_ptr && decoded_len > 0) {
    payload_src = decoded_ptr;
    payload_len = decoded_len;
    needs_encrypt = true;
  } else if (encrypted_ptr && encrypted_len > 0) {
    payload_src = encrypted_ptr;
    payload_len = encrypted_len;
    needs_encrypt = false;
  } else {
    ESP_LOGW(TAG, "TX app: MeshPacket sem decoded nem encrypted (len=%u)", mp_len);
    return ESP_ERR_INVALID_ARG;
  }

  if (payload_len > MT_MAX_PAYLOAD) {
    ESP_LOGW(TAG, "TX app: payload muito grande (%u)", payload_len);
    return ESP_ERR_INVALID_SIZE;
  }

  mesh_packet_header_t hdr = {0};
  hdr.from = from;
  hdr.to = to;
  hdr.id = pkt_id;
  hdr.flags = (hop_limit & MT_HOP_LIMIT_MASK) | (want_ack ? MT_WANT_ACK_MASK : 0) |
              ((hop_limit << MT_HOP_START_SHIFT) & MT_HOP_START_MASK);
  hdr.channel = channel;
  hdr.relay_node = (uint8_t)(s_node_num & 0xFF);

  uint8_t wire[256];
  memcpy(wire, &hdr, MT_HEADER_SIZE);
  memcpy(wire + MT_HEADER_SIZE, payload_src, payload_len);
  if (needs_encrypt) {
    mesh_crypt(from, pkt_id, wire + MT_HEADER_SIZE, payload_len);
  }
  uint16_t wire_len = MT_HEADER_SIZE + payload_len;

  dedup_add(from, pkt_id);

  uint8_t prio = priority_for_portnum(portnum_hint);
  ESP_LOGI(TAG,
           "TX app: to=0x%08lX ch=%u hop=%u %s id=0x%08lX prio=%u port=%lu len=%u",
           (unsigned long)to,
           channel,
           hop_limit,
           needs_encrypt ? "decoded" : "encrypted",
           (unsigned long)pkt_id,
           prio,
           (unsigned long)portnum_hint,
           wire_len);

  return tx_queue_push(wire, wire_len, prio, 0) ? ESP_OK : ESP_FAIL;
}

static void on_rx_done(const sx1262_packet_t *pkt, void *ctx) {
  (void)ctx;
  if (pkt == NULL)
    return;
  if (pkt->has_crc_error || pkt->has_header_error) {
    ESP_LOGW(TAG, "RX: erro HW, skip");

    if (s_is_running)
      sx1262_receive_continuous();
    return;
  }

  process_rx_packet(pkt);

  if (s_is_running)
    sx1262_receive_continuous();
}

static void on_tx_done(void *ctx) {
  (void)ctx;
  ESP_LOGD(TAG, "TX done");
}

static void on_timeout(void *ctx) {
  (void)ctx;
  if (s_is_running)
    sx1262_receive_continuous();
}

static void on_cad_done(bool is_channel_active, void *ctx) {
  (void)ctx;
  if (s_mesh_task_handle) {
    BaseType_t higher_prio = pdFALSE;
    uint32_t bit = is_channel_active ? CAD_NOTIFY_BUSY : CAD_NOTIFY_CLEAR;
    xTaskNotifyFromISR(s_mesh_task_handle, bit, eSetBits, &higher_prio);
    portYIELD_FROM_ISR(higher_prio);
  }
}

static int csma_cad_check(void) {
  ulTaskNotifyValueClear(NULL, CAD_NOTIFY_BUSY | CAD_NOTIFY_CLEAR);

  esp_err_t rc = sx1262_cad_start();
  if (rc != ESP_OK) {
    ESP_LOGW(TAG, "CAD start falhou: %s", esp_err_to_name(rc));
    return -1;
  }

  uint32_t notify = 0;
  BaseType_t got = xTaskNotifyWait(
      0, CAD_NOTIFY_BUSY | CAD_NOTIFY_CLEAR, &notify, pdMS_TO_TICKS(CAD_TIMEOUT_MS));
  if (got != pdTRUE) {
    ESP_LOGW(TAG, "CAD timeout");
    return -1;
  }
  if (notify & CAD_NOTIFY_BUSY)
    return 1;
  if (notify & CAD_NOTIFY_CLEAR)
    return 0;
  return -1;
}

static void mesh_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "Mesh task iniciada (CSMA/CA habilitado)");

  while (s_is_running) {
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    mesh_tx_entry_t tx;
    if (tx_queue_pop_highest(&tx, now)) {
      bool tx_ok = false;
      for (int retry = 0; retry < CSMA_MAX_RETRIES; retry++) {
        sx1262_stop_rx();

        int cad = csma_cad_check();
        if (cad == 0) {
          esp_err_t rc = sx1262_transmit(tx.buf, (uint8_t)tx.len, 5000);
          if (rc != ESP_OK) {
            ESP_LOGW(TAG, "TX falhou: %s (prio=%d)", esp_err_to_name(rc), tx.priority);
          } else {
            ESP_LOGI(TAG, "TX: %d bytes prio=%d cad_retry=%d", tx.len, tx.priority, retry);
          }

          for (int i = 0; i < 500; i++) {
            sx1262_process_irq();
            if (sx1262_get_state() == SX1262_STATE_STDBY_RC)
              break;
            vTaskDelay(1);
          }
          tx_ok = true;
          break;
        }

        uint32_t backoff = CSMA_BACKOFF_BASE_MS * (1U << retry);
        backoff += esp_random() % CSMA_BACKOFF_JITTER_MS;

        ESP_LOGI(TAG,
                 "CSMA: canal %s, retry %d/%d, backoff %lu ms",
                 cad == 1 ? "busy" : "erro",
                 retry + 1,
                 CSMA_MAX_RETRIES,
                 (unsigned long)backoff);

        sx1262_receive_continuous();
        vTaskDelay(pdMS_TO_TICKS(backoff));
      }

      if (!tx_ok) {
        ESP_LOGW(TAG,
                 "TX desistido apos %d CADs (prio=%d len=%d)",
                 CSMA_MAX_RETRIES,
                 tx.priority,
                 tx.len);
      }

      sx1262_receive_continuous();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ESP_LOGI(TAG, "Mesh task terminada");
  s_mesh_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t meshtastic_mesh_init(uint32_t node_num) {
  s_node_num = node_num;
  mt_pkt_history_init();
  mt_channels_init();

  memset(s_tx_queue, 0, sizeof(s_tx_queue));
  if (!s_tx_mutex) {
    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex)
      return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG,
           "Mesh init: node=0x%08lX, PSK=default AES-128, queue=%d slots",
           (unsigned long)node_num,
           MESH_TX_QUEUE_SIZE);
  return ESP_OK;
}

esp_err_t meshtastic_mesh_start(void) {
  if (s_is_running)
    return ESP_ERR_INVALID_STATE;

  sx1262_callbacks_t cbs = {
      .on_rx_done = on_rx_done,
      .on_tx_done = on_tx_done,
      .on_timeout = on_timeout,
      .on_cad_done = on_cad_done,
      .on_error = NULL,
      .cb_ctx = NULL,
  };
  sx1262_set_callbacks(&cbs);

  esp_err_t ret = sx1262_receive_continuous();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "RX continuous falhou: %s", esp_err_to_name(ret));
    return ret;
  }

  s_is_running = true;

  BaseType_t ok =
      xTaskCreate(mesh_task, "mesh_tx", 4096, NULL, tskIDLE_PRIORITY + 3, &s_mesh_task_handle);
  if (ok != pdPASS) {
    s_is_running = false;
    ESP_LOGE(TAG, "Falha criando mesh task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Mesh start: RX continuous + TX task ativa");
  return ESP_OK;
}

void meshtastic_mesh_stop(void) {
  if (!s_is_running)
    return;
  s_is_running = false;
  sx1262_stop_rx();

  ESP_LOGI(TAG, "Mesh stop solicitado");
}

void meshtastic_mesh_poll(void) {
  if (s_is_running) {
    sx1262_process_irq();
  }
}

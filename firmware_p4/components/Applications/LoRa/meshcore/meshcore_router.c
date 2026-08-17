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

#include "meshcore_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sodium.h"
#include "sx1262.h"
#include "sx1262_regs.h"

static const char *TAG = "MC_ROUTER";

typedef struct {
  bool is_used;
  uint32_t tx_at_ms;
  uint16_t len;
  uint8_t buf[MESHCORE_MAX_RAW];
} mc_relay_slot_t;

static bool s_is_relay_enabled = false;
static bool s_is_packet_consumed = false;
static uint32_t s_last_advert_ms = 0;

static mc_relay_slot_t s_relay_queue[MC_RELAY_QUEUE_SIZE];

static volatile bool s_is_cad_pending = false;
static volatile bool s_is_cad_busy = false;

static void process_advert(const meshcore_packet_view_t *pkt);
static void process_grp_txt(const meshcore_packet_view_t *pkt);
static void process_txt_msg(const meshcore_packet_view_t *pkt);
static void process_ack(const meshcore_packet_view_t *pkt);
static void process_path_payload(const meshcore_packet_view_t *pkt);

static void on_rx_done(const sx1262_packet_t *pkt, void *ctx);
static void on_tx_done(void *ctx);
static void on_timeout(void *ctx);
static void on_cad_done(bool is_active, void *ctx);

static bool cad_check_clear(void);

static void relay_enqueue(const uint8_t *buf, uint16_t len, uint32_t delay_ms);
static void try_relay(const meshcore_packet_view_t *view, const uint8_t *raw, uint16_t raw_len);

esp_err_t meshcore_send_advert(void) {
  int32_t lat, lon;
  bool has;
  mc_get_advert_latlon_internal(&lat, &lon, &has);

  uint8_t buf[256];
  uint16_t n = meshcore_packet_build_advert(
      mc_get_identity(), lat, lon, has, meshcore_get_unix_time(), buf, sizeof(buf));
  if (n == 0)
    return ESP_FAIL;
  ESP_LOGI(TAG, "TX ADVERT: %u bytes", n);
  s_last_advert_ms = mc_now_ms();
  return mc_transmit_raw(buf, n);
}

esp_err_t meshcore_send_advert_throttled(void) {
  uint32_t t = mc_now_ms();
  if (s_last_advert_ms != 0 && (t - s_last_advert_ms) < MC_ADVERT_COOLDOWN_MS) {
    return ESP_ERR_INVALID_STATE;
  }
  return meshcore_send_advert();
}

esp_err_t meshcore_send_grp_txt(uint8_t channel_idx, const char *text) {
  if (channel_idx >= MESHCORE_MAX_CHANNELS)
    return ESP_ERR_INVALID_ARG;
  const meshcore_channel_t *ch = meshcore_channel_get(channel_idx);
  if (ch == NULL)
    return ESP_ERR_NOT_FOUND;
  if (text == NULL)
    return ESP_ERR_INVALID_ARG;

  uint8_t shared[32];
  memset(shared, 0, 32);
  memcpy(shared, ch->secret, 16);

  uint8_t buf[256];
  uint16_t n = meshcore_packet_build_grp_txt(
      ch->hash, shared, mc_get_identity()->name, text, meshcore_get_unix_time(), buf, sizeof(buf));
  if (n == 0)
    return ESP_FAIL;
  ESP_LOGI(TAG, "TX GRP_TXT[%u]: %u bytes ('%s')", channel_idx, n, text);
  return mc_transmit_raw(buf, n);
}

esp_err_t meshcore_send_direct_msg(const uint8_t peer_pub_key[32],
                                   const char *text,
                                   uint32_t timestamp,
                                   uint8_t attempt,
                                   uint32_t *out_expected_ack) {
  if (peer_pub_key == NULL || text == NULL)
    return ESP_ERR_INVALID_ARG;
  int text_len = (int)strlen(text);
  if (text_len > MESHCORE_TEXT_MAX)
    text_len = MESHCORE_TEXT_MAX;

  const meshcore_identity_t *id = mc_get_identity();

  uint8_t plaintext[5 + MESHCORE_TEXT_MAX + 1];
  memcpy(&plaintext[0], &timestamp, 4);
  plaintext[4] = (attempt & 3);
  memcpy(&plaintext[5], text, text_len);
  plaintext[5 + text_len] = 0;
  int pt_len = 5 + text_len + 1;

  uint32_t ack_crc;
  mc_sha256_two((uint8_t *)&ack_crc, 4, plaintext, 5 + text_len, id->pub_key, 32);
  if (out_expected_ack != NULL)
    *out_expected_ack = ack_crc;

  uint8_t shared[32];
  mc_x25519(shared, peer_pub_key, id->priv_key);

  const meshcore_contact_t *c = meshcore_contact_find(peer_pub_key);
  bool has_path = (c != NULL && c->out_path_len != MESHCORE_OUT_PATH_UNKNOWN);
  uint8_t route_type = has_path ? MC_RT_DIRECT : MC_RT_FLOOD;

  uint8_t buf[256];
  uint16_t pos = 0;
  buf[pos++] = mc_build_header(MC_VERSION_V1, MC_PT_TXT_MSG, route_type);

  if (has_path) {
    buf[pos++] = c->out_path_len;
    uint8_t hash_count = c->out_path_len & 0x3F;
    uint8_t hash_size = ((c->out_path_len >> 6) & 0x03) + 1;
    uint16_t path_bytes_total = (uint16_t)hash_count * hash_size;
    memcpy(&buf[pos], c->out_path, path_bytes_total);
    pos += path_bytes_total;
  } else {
    buf[pos++] = mc_build_path_len(MC_HASH_SIZE_1B, 0);
  }

  buf[pos++] = peer_pub_key[0];
  buf[pos++] = id->pub_key[0];

  int enc_written = meshcore_crypto_encrypt_mac(shared, &buf[pos], plaintext, pt_len);
  pos += enc_written;

  mc_pendings_add(ack_crc, peer_pub_key);

  ESP_LOGI(TAG,
           "TX DM %s dst=%02X%02X%02X%02X len=%d ACK_CRC=0x%08lX",
           has_path ? "DIRECT" : "FLOOD",
           peer_pub_key[0],
           peer_pub_key[1],
           peer_pub_key[2],
           peer_pub_key[3],
           text_len,
           (unsigned long)ack_crc);

  return mc_transmit_raw(buf, pos);
}

void meshcore_set_relay(bool is_enabled) {
  s_is_relay_enabled = is_enabled;
  ESP_LOGI(TAG, "Relay %s", is_enabled ? "ENABLED" : "disabled");
}

bool meshcore_get_relay(void) {
  return s_is_relay_enabled;
}

void mc_router_init(void) {
  s_is_relay_enabled = false;
  s_is_packet_consumed = false;
  s_last_advert_ms = 0;
  memset(s_relay_queue, 0, sizeof(s_relay_queue));

  sx1262_callbacks_t cbs = {
      .on_rx_done = on_rx_done,
      .on_tx_done = on_tx_done,
      .on_timeout = on_timeout,
      .on_cad_done = on_cad_done,
      .on_error = NULL,
      .cb_ctx = NULL,
  };
  sx1262_set_callbacks(&cbs);
}

void mc_router_set_consumed(bool is_consumed) {
  s_is_packet_consumed = is_consumed;
}

void mc_router_mark_advert_sent(void) {
  s_last_advert_ms = mc_now_ms();
}

void mc_router_relay_process(void) {
  uint32_t t = mc_now_ms();
  for (int i = 0; i < MC_RELAY_QUEUE_SIZE; i++) {
    if (s_relay_queue[i].is_used && (int32_t)(t - s_relay_queue[i].tx_at_ms) >= 0) {
      ESP_LOGI(TAG, "Relay TX (%u bytes)", s_relay_queue[i].len);
      mc_transmit_raw(s_relay_queue[i].buf, s_relay_queue[i].len);
      s_relay_queue[i].is_used = false;
      return;
    }
  }
}

esp_err_t mc_transmit_raw(const uint8_t *raw, uint16_t len) {
  if (len > MESHCORE_MAX_RAW)
    return ESP_ERR_INVALID_SIZE;

  meshcore_packet_view_t self_view;
  if (meshcore_packet_parse(raw, len, &self_view)) {
    uint8_t self_hash[8];
    meshcore_packet_hash(&self_view, self_hash);
    mc_dedup_check_add(self_hash);
  }

  for (int attempt = 0; attempt < MC_CAD_MAX_RETRIES; attempt++) {
    if (cad_check_clear())
      break;
    uint32_t delay = MC_RELAY_DELAY_MIN_MS + (esp_random() % MC_RELAY_DELAY_RANGE_MS);
    vTaskDelay(pdMS_TO_TICKS(delay));
    if (attempt + 1 == MC_CAD_MAX_RETRIES) {
      ESP_LOGW(TAG, "CAD: %d attempts busy -- TX anyway", MC_CAD_MAX_RETRIES);
    }
  }

  sx1262_stop_rx();
  esp_err_t ret = sx1262_transmit(raw, (uint8_t)len, 5000);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "TX failed: %s", esp_err_to_name(ret));
    if (mc_is_running())
      sx1262_receive_continuous();
    return ret;
  }
  for (int i = 0; i < 500; i++) {
    sx1262_process_irq();
    if (sx1262_get_state() == SX1262_STATE_STDBY_RC)
      break;
    vTaskDelay(1);
  }
  if (mc_is_running())
    sx1262_receive_continuous();
  return ESP_OK;
}

esp_err_t mc_send_path_return(const uint8_t peer_pub_key[32],
                              const uint8_t *path_bytes,
                              uint8_t path_count,
                              uint8_t hash_size,
                              uint8_t extra_type,
                              const uint8_t *extra,
                              size_t extra_len) {
  if (peer_pub_key == NULL)
    return ESP_ERR_INVALID_ARG;

  const meshcore_identity_t *id = mc_get_identity();

  uint8_t shared[32];
  mc_x25519(shared, peer_pub_key, id->priv_key);

  uint8_t plaintext[2 + MESHCORE_MAX_PATH + 1 + 16];
  int pt_len = 0;
  uint8_t hash_size_sel = (uint8_t)((hash_size - 1) & 0x03);
  plaintext[pt_len++] = mc_build_path_len(hash_size_sel, path_count);
  uint16_t path_bytes_total = (uint16_t)path_count * hash_size;
  if (path_bytes_total > MESHCORE_MAX_PATH)
    return ESP_ERR_INVALID_SIZE;
  memcpy(&plaintext[pt_len], path_bytes, path_bytes_total);
  pt_len += path_bytes_total;
  if (extra_len > 0 && extra != NULL) {
    plaintext[pt_len++] = extra_type & 0x0F;
    memcpy(&plaintext[pt_len], extra, extra_len);
    pt_len += extra_len;
  } else {
    plaintext[pt_len++] = 0xFF;
    for (int i = 0; i < 4; i++)
      plaintext[pt_len++] = (uint8_t)esp_random();
  }

  uint8_t buf[256];
  uint16_t pos = 0;
  buf[pos++] = mc_build_header(MC_VERSION_V1, MC_PT_PATH, MC_RT_FLOOD);
  buf[pos++] = mc_build_path_len(MC_HASH_SIZE_1B, 0);
  buf[pos++] = peer_pub_key[0];
  buf[pos++] = id->pub_key[0];
  int enc_written = meshcore_crypto_encrypt_mac(shared, &buf[pos], plaintext, pt_len);
  pos += enc_written;

  ESP_LOGI(TAG,
           "TX PATH return -> %02X%02X%02X%02X count=%u %s",
           peer_pub_key[0],
           peer_pub_key[1],
           peer_pub_key[2],
           peer_pub_key[3],
           path_count,
           extra_len > 0 ? "(+ACK)" : "");
  return mc_transmit_raw(buf, pos);
}

static void process_advert(const meshcore_packet_view_t *pkt) {
  if (pkt->payload_len < 32 + 4 + 64 + 1) {
    ESP_LOGW(TAG, "ADVERT too short (%u)", pkt->payload_len);
    return;
  }
  const uint8_t *p = pkt->payload;
  const uint8_t *pubkey = p;
  uint32_t ts =
      (uint32_t)p[32] | ((uint32_t)p[33] << 8) | ((uint32_t)p[34] << 16) | ((uint32_t)p[35] << 24);
  const uint8_t *sig = p + 32 + 4;
  const uint8_t *app_data = p + 32 + 4 + 64;
  uint16_t app_len = pkt->payload_len - (32 + 4 + 64);

  uint16_t sig_app_len = app_len > 32 ? 32 : app_len;
  uint8_t sign_buf[32 + 4 + 32];
  memcpy(&sign_buf[0], pubkey, 32);
  memcpy(&sign_buf[32], &p[32], 4);
  memcpy(&sign_buf[36], app_data, sig_app_len);
  if (crypto_sign_verify_detached(sig, sign_buf, 36 + sig_app_len, pubkey) != 0) {
    ESP_LOGW(TAG,
             "ADVERT invalid sig (%02X%02X%02X%02X) -- discard",
             pubkey[0],
             pubkey[1],
             pubkey[2],
             pubkey[3]);
    return;
  }

  char name[33] = "?";
  int32_t lat_e6 = 0, lon_e6 = 0;
  uint8_t adv_type = 0;
  if (app_len >= 1) {
    uint8_t flags = app_data[0];
    adv_type = flags & 0x0F;
    uint16_t off = 1;
    if (flags & MESHCORE_ADV_FLAG_HAS_LATLON) {
      if (off + 8 <= app_len) {
        memcpy(&lat_e6, &app_data[off], 4);
        off += 4;
        memcpy(&lon_e6, &app_data[off], 4);
        off += 4;
      } else {
        off = app_len;
      }
    }
    if ((flags & MESHCORE_ADV_FLAG_HAS_NAME) && off < app_len) {
      uint16_t nl = app_len - off;
      if (nl > 32)
        nl = 32;
      memcpy(name, &app_data[off], nl);
      name[nl] = 0;
    }
  }

  if (memcmp(pubkey, mc_get_identity()->pub_key, 32) == 0) {
    return;
  }

  meshcore_contact_t c;
  memset(&c, 0, sizeof(c));
  c.is_used = true;
  memcpy(c.pub_key, pubkey, 32);
  c.type = (adv_type >= 1 && adv_type <= 4) ? adv_type : MC_ADV_TYPE_CHAT;
  c.flags = 0;
  c.out_path_len = MESHCORE_OUT_PATH_UNKNOWN;
  strncpy(c.name, name, MESHCORE_NAME_MAX - 1);
  c.last_advert = ts;
  c.gps_lat = lat_e6;
  c.gps_lon = lon_e6;
  c.lastmod = meshcore_get_unix_time();
  meshcore_contact_upsert(&c);

  const meshcore_callbacks_t *cbs = mc_get_callbacks();
  if (cbs->on_advert != NULL) {
    const meshcore_contact_t *stored = meshcore_contact_find(pubkey);
    if (stored != NULL) {
      cbs->on_advert(stored, pkt->rssi_dbm, pkt->snr_db, cbs->ctx);
    }
  }
}

static void process_grp_txt(const meshcore_packet_view_t *pkt) {
  if (pkt->payload_len < 1 + 2 + 16) {
    ESP_LOGW(TAG, "GRP_TXT too short (%u)", pkt->payload_len);
    return;
  }
  uint8_t ch_hash = pkt->payload[0];
  const meshcore_channel_t *channels = mc_channels_array();

  for (int idx = 0; idx < MESHCORE_MAX_CHANNELS; idx++) {
    if (!channels[idx].is_used)
      continue;
    if (channels[idx].hash != ch_hash)
      continue;

    uint8_t shared[32];
    memset(shared, 0, 32);
    memcpy(shared, channels[idx].secret, 16);

    uint8_t plaintext[160];
    int pt_len =
        meshcore_crypto_mac_decrypt(shared, plaintext, pkt->payload + 1, pkt->payload_len - 1);
    if (pt_len <= 5)
      continue;

    uint32_t ts = (uint32_t)plaintext[0] | ((uint32_t)plaintext[1] << 8) |
                  ((uint32_t)plaintext[2] << 16) | ((uint32_t)plaintext[3] << 24);
    uint8_t txt_type = plaintext[4] >> 2;
    if (txt_type != MC_TXT_TYPE_PLAIN) {
      return;
    }

    int text_len = pt_len - 5;
    while (text_len > 0 && plaintext[5 + text_len - 1] == 0)
      text_len--;
    char text[160];
    if (text_len > (int)sizeof(text) - 1)
      text_len = sizeof(text) - 1;
    memcpy(text, &plaintext[5], text_len);
    text[text_len] = 0;

    const meshcore_callbacks_t *cbs = mc_get_callbacks();
    if (cbs->on_grp_txt != NULL) {
      cbs->on_grp_txt(
          (uint8_t)idx, pkt->path_count, text, ts, pkt->rssi_dbm, pkt->snr_db, cbs->ctx);
    }
    return;
  }
}

static void process_txt_msg(const meshcore_packet_view_t *pkt) {
  if (pkt->payload_len < 1 + 1 + 2 + 16) {
    ESP_LOGW(TAG, "TXT_MSG too short (%u)", pkt->payload_len);
    return;
  }
  uint8_t dest_hash = pkt->payload[0];
  uint8_t src_hash = pkt->payload[1];

  const meshcore_identity_t *id = mc_get_identity();
  if (dest_hash != id->pub_key[0]) {
    return;
  }

  meshcore_contact_t *contacts = mc_contacts_array_mut();
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (!contacts[i].is_used)
      continue;
    if (contacts[i].pub_key[0] != src_hash)
      continue;

    uint8_t shared[32];
    mc_x25519(shared, contacts[i].pub_key, id->priv_key);

    uint8_t plaintext[160];
    int pt_len =
        meshcore_crypto_mac_decrypt(shared, plaintext, pkt->payload + 2, pkt->payload_len - 2);
    if (pt_len <= 5)
      continue;

    uint32_t ts = (uint32_t)plaintext[0] | ((uint32_t)plaintext[1] << 8) |
                  ((uint32_t)plaintext[2] << 16) | ((uint32_t)plaintext[3] << 24);
    uint8_t flags = plaintext[4];
    uint8_t attempt = flags & 0x03;
    uint8_t txt_type = flags >> 2;
    (void)attempt;

    const char *text_start = (const char *)&plaintext[5];
    int text_len = pt_len - 5;
    char text[160];

    if (txt_type == MC_TXT_TYPE_SIGNED_PLAIN) {
      if (text_len < 4) {
        ESP_LOGW(TAG, "DM SIGNED_PLAIN truncated");
        return;
      }
      text_start += 4;
      text_len -= 4;
    }

    while (text_len > 0 && text_start[text_len - 1] == 0)
      text_len--;
    if (text_len > (int)sizeof(text) - 1)
      text_len = sizeof(text) - 1;
    memcpy(text, text_start, text_len);
    text[text_len] = 0;

    ESP_LOGI(TAG,
             "DM RX from %02X%02X%02X%02X txt_type=%u: \"%s\"",
             contacts[i].pub_key[0],
             contacts[i].pub_key[1],
             contacts[i].pub_key[2],
             contacts[i].pub_key[3],
             txt_type,
             text);

    uint8_t crc_text_off = 5;
    if (txt_type == MC_TXT_TYPE_SIGNED_PLAIN)
      crc_text_off = 9;
    int crc_text_len = (int)strlen((char *)&plaintext[crc_text_off]);
    if (crc_text_len > pt_len - crc_text_off)
      crc_text_len = pt_len - crc_text_off;

    uint32_t ack_crc;
    mc_sha256_two(
        (uint8_t *)&ack_crc, 4, plaintext, crc_text_off + crc_text_len, contacts[i].pub_key, 32);

    s_is_packet_consumed = true;

    const meshcore_callbacks_t *cbs = mc_get_callbacks();
    if (cbs->on_direct_msg != NULL) {
      cbs->on_direct_msg(contacts[i].pub_key,
                         text,
                         ts,
                         txt_type,
                         pkt->path_count,
                         pkt->rssi_dbm,
                         pkt->snr_db,
                         cbs->ctx);
    }

    if (pkt->route_type == MC_RT_FLOOD || pkt->route_type == MC_RT_TRANSPORT_FLOOD) {
      mc_send_path_return(contacts[i].pub_key,
                          pkt->path,
                          pkt->path_count,
                          pkt->hash_size,
                          MC_PT_ACK,
                          (const uint8_t *)&ack_crc,
                          4);
    } else {
      uint8_t ack_buf[16];
      uint8_t ack_pos = 0;
      ack_buf[ack_pos++] = mc_build_header(MC_VERSION_V1, MC_PT_ACK, MC_RT_FLOOD);
      ack_buf[ack_pos++] = mc_build_path_len(MC_HASH_SIZE_1B, 0);
      memcpy(&ack_buf[ack_pos], &ack_crc, 4);
      ack_pos += 4;
      mc_transmit_raw(ack_buf, ack_pos);
    }
    return;
  }

  ESP_LOGW(TAG, "DM dest=0x%02X src=0x%02X but sender unknown", dest_hash, src_hash);
}

static void process_ack(const meshcore_packet_view_t *pkt) {
  if (pkt->payload_len < 4)
    return;
  uint32_t crc;
  memcpy(&crc, pkt->payload, 4);

  uint8_t peer_pub[32];
  if (mc_pendings_match(crc, peer_pub)) {
    ESP_LOGI(TAG, "ACK matched pending CRC=0x%08lX", (unsigned long)crc);
    const meshcore_callbacks_t *cbs = mc_get_callbacks();
    if (cbs->on_ack != NULL)
      cbs->on_ack(crc, peer_pub, cbs->ctx);
    s_is_packet_consumed = true;
  }
}

static void process_path_payload(const meshcore_packet_view_t *pkt) {
  if (pkt->payload_len < 4 + 16)
    return;
  uint8_t dst_hash = pkt->payload[0];
  uint8_t src_hash = pkt->payload[1];

  const meshcore_identity_t *id = mc_get_identity();
  if (dst_hash != id->pub_key[0])
    return;

  meshcore_contact_t *contacts = mc_contacts_array_mut();
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (!contacts[i].is_used)
      continue;
    if (contacts[i].pub_key[0] != src_hash)
      continue;

    uint8_t shared[32];
    mc_x25519(shared, contacts[i].pub_key, id->priv_key);

    uint8_t plaintext[200];
    int pt_len =
        meshcore_crypto_mac_decrypt(shared, plaintext, pkt->payload + 2, pkt->payload_len - 2);
    if (pt_len <= 0)
      continue;

    uint8_t pl_byte = plaintext[0];
    uint8_t hash_count = pl_byte & 0x3F;
    uint8_t hash_size = mc_parse_hash_size((pl_byte >> 6) & 0x03);
    if (hash_size == 0)
      continue;

    uint16_t path_bytes_total = (uint16_t)hash_count * hash_size;
    if (1 + path_bytes_total > pt_len)
      continue;
    if (path_bytes_total > MESHCORE_MAX_PATH)
      continue;

    contacts[i].out_path_len = pl_byte;
    memset(contacts[i].out_path, 0, MESHCORE_MAX_PATH);
    if (path_bytes_total > 0) {
      memcpy(contacts[i].out_path, &plaintext[1], path_bytes_total);
    }
    contacts[i].lastmod = meshcore_get_unix_time();
    mc_contacts_persist();
    s_is_packet_consumed = true;

    ESP_LOGI(TAG,
             "PATH inbound: %02X%02X%02X%02X learned (count=%u size=%u)",
             contacts[i].pub_key[0],
             contacts[i].pub_key[1],
             contacts[i].pub_key[2],
             contacts[i].pub_key[3],
             hash_count,
             hash_size);

    const meshcore_callbacks_t *cbs = mc_get_callbacks();
    if (cbs->on_path_update != NULL) {
      cbs->on_path_update(&contacts[i], cbs->ctx);
    }

    int extras_off = 1 + path_bytes_total;
    if (extras_off + 1 <= pt_len) {
      uint8_t extra_type = plaintext[extras_off] & 0x0F;
      int extra_len = pt_len - (extras_off + 1);
      if (extra_type == MC_PT_ACK && extra_len >= 4) {
        uint32_t crc;
        memcpy(&crc, &plaintext[extras_off + 1], 4);
        uint8_t peer_pub[32];
        if (mc_pendings_match(crc, peer_pub)) {
          ESP_LOGI(TAG, "PATH+ACK piggyback CRC=0x%08lX", (unsigned long)crc);
          if (cbs->on_ack != NULL)
            cbs->on_ack(crc, peer_pub, cbs->ctx);
        }
      }
    }
    return;
  }
}

static void on_rx_done(const sx1262_packet_t *pkt, void *ctx) {
  (void)ctx;
  if (pkt == NULL)
    return;
  if (pkt->has_crc_error || pkt->has_header_error) {
    ESP_LOGW(TAG, "RX HW error -- skip");
    return;
  }

  meshcore_packet_view_t view;
  if (!meshcore_packet_parse(pkt->buf, pkt->len, &view)) {
    ESP_LOGW(TAG, "RX parse failed (%d bytes)", pkt->len);
    return;
  }
  view.rssi_dbm = pkt->rssi_pkt_dbm;
  view.snr_db = pkt->snr_pkt_db;

  uint8_t hash[8];
  meshcore_packet_hash(&view, hash);
  if (mc_dedup_check_add(hash))
    return;

  s_is_packet_consumed = false;
  switch (view.payload_type) {
    case MC_PT_ADVERT:
      process_advert(&view);
      break;
    case MC_PT_GRP_TXT:
      process_grp_txt(&view);
      break;
    case MC_PT_TXT_MSG:
      process_txt_msg(&view);
      break;
    case MC_PT_ACK:
      process_ack(&view);
      break;
    case MC_PT_PATH:
      process_path_payload(&view);
      break;
    default:
      break;
  }

  if (!s_is_packet_consumed) {
    try_relay(&view, pkt->buf, pkt->len);
  }

  if (mc_is_running())
    sx1262_receive_continuous();
}

static void on_tx_done(void *ctx) {
  (void)ctx;
}

static void on_timeout(void *ctx) {
  (void)ctx;
  if (mc_is_running())
    sx1262_receive_continuous();
}

static void on_cad_done(bool is_active, void *ctx) {
  (void)ctx;
  s_is_cad_busy = is_active;
  s_is_cad_pending = false;
}

static bool cad_check_clear(void) {
  s_is_cad_pending = true;
  s_is_cad_busy = false;

  sx1262_stop_rx();
  esp_err_t err = sx1262_cad_start();
  if (err != ESP_OK) {
    s_is_cad_pending = false;
    return true;
  }

  uint32_t deadline = mc_now_ms() + MC_CAD_TIMEOUT_MS;
  while (s_is_cad_pending && (int32_t)(mc_now_ms() - deadline) < 0) {
    sx1262_process_irq();
    vTaskDelay(1);
  }

  if (s_is_cad_pending) {
    ESP_LOGW(TAG, "CAD timeout -- assume clear");
    s_is_cad_pending = false;
    return true;
  }

  return !s_is_cad_busy;
}

static void relay_enqueue(const uint8_t *buf, uint16_t len, uint32_t delay_ms) {
  for (int i = 0; i < MC_RELAY_QUEUE_SIZE; i++) {
    if (!s_relay_queue[i].is_used) {
      memcpy(s_relay_queue[i].buf, buf, len);
      s_relay_queue[i].len = len;
      s_relay_queue[i].tx_at_ms = mc_now_ms() + delay_ms;
      s_relay_queue[i].is_used = true;
      return;
    }
  }
  ESP_LOGW(TAG, "Relay queue full -- discard");
}

static void try_relay(const meshcore_packet_view_t *view, const uint8_t *raw, uint16_t raw_len) {
  if (!s_is_relay_enabled)
    return;

  bool is_flood = (view->route_type == MC_RT_FLOOD || view->route_type == MC_RT_TRANSPORT_FLOOD);
  bool is_direct = (view->route_type == MC_RT_DIRECT || view->route_type == MC_RT_TRANSPORT_DIRECT);
  if (!is_flood && !is_direct)
    return;

  uint8_t hash_size = view->hash_size;
  uint8_t path_count = view->path_count;
  const meshcore_identity_t *id = mc_get_identity();

  if (is_direct) {
    if (path_count == 0)
      return;
    if (memcmp(view->path, id->pub_key, hash_size) != 0)
      return;
  } else {
    if ((uint16_t)((path_count + 1) * hash_size) > MESHCORE_MAX_PATH)
      return;
  }

  uint8_t new_buf[MESHCORE_MAX_RAW + 8];
  uint16_t pos = 0;
  uint16_t src_off = 0;

  new_buf[pos++] = raw[src_off++];

  if (view->route_type == MC_RT_TRANSPORT_FLOOD || view->route_type == MC_RT_TRANSPORT_DIRECT) {
    memcpy(&new_buf[pos], &raw[src_off], 4);
    pos += 4;
    src_off += 4;
  }

  uint8_t hash_size_sel = (uint8_t)((hash_size - 1) & 0x03);
  src_off++;
  uint16_t existing_path_bytes = (uint16_t)path_count * hash_size;

  if (is_flood) {
    new_buf[pos++] = mc_build_path_len(hash_size_sel, path_count + 1);
    memcpy(&new_buf[pos], &raw[src_off], existing_path_bytes);
    pos += existing_path_bytes;
    src_off += existing_path_bytes;
    memcpy(&new_buf[pos], id->pub_key, hash_size);
    pos += hash_size;
  } else {
    new_buf[pos++] = mc_build_path_len(hash_size_sel, path_count - 1);
    if (existing_path_bytes > hash_size) {
      memcpy(&new_buf[pos], &raw[src_off + hash_size], existing_path_bytes - hash_size);
      pos += existing_path_bytes - hash_size;
    }
    src_off += existing_path_bytes;
  }

  if (src_off < raw_len) {
    uint16_t payload_bytes = raw_len - src_off;
    memcpy(&new_buf[pos], &raw[src_off], payload_bytes);
    pos += payload_bytes;
  }

  if (pos > MESHCORE_MAX_RAW) {
    ESP_LOGW(TAG, "Relay: reassembled packet exceeds MAX_RAW");
    return;
  }

  uint32_t delay = MC_RELAY_DELAY_MIN_MS + (esp_random() % MC_RELAY_DELAY_RANGE_MS);
  relay_enqueue(new_buf, pos, delay);
  ESP_LOGI(TAG,
           "Relay %s scheduled type=0x%X count %u->%u in %lums",
           is_flood ? "FLOOD" : "DIRECT",
           view->payload_type,
           path_count,
           is_flood ? (path_count + 1) : (path_count - 1),
           (unsigned long)delay);
}

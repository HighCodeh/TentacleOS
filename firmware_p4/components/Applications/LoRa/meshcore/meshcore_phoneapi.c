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

#include "meshcore_phoneapi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"

#include "meshcore.h"
#include "meshcore_internal.h"
#include "meshcore_nvs.h"

static const char *TAG = "MC_PHONEAPI";

#define CMD_APP_START            0x01
#define CMD_SEND_TXT_MSG         0x02
#define CMD_SEND_CHANNEL_TXT_MSG 0x03
#define CMD_GET_CONTACTS         0x04
#define CMD_GET_DEVICE_TIME      0x05
#define CMD_SET_DEVICE_TIME      0x06
#define CMD_SEND_SELF_ADVERT     0x07
#define CMD_SET_ADVERT_NAME      0x08
#define CMD_ADD_UPDATE_CONTACT   0x09
#define CMD_SYNC_NEXT_MESSAGE    0x0A
#define CMD_SET_RADIO_PARAMS     0x0B
#define CMD_SET_RADIO_TX_POWER   0x0C
#define CMD_RESET_PATH           0x0D
#define CMD_SET_ADVERT_LATLON    0x0E
#define CMD_REMOVE_CONTACT       0x0F
#define CMD_SHARE_CONTACT        0x10
#define CMD_EXPORT_CONTACT       0x11
#define CMD_IMPORT_CONTACT       0x12
#define CMD_REBOOT               0x13
#define CMD_GET_BATT_AND_STORAGE 0x14
#define CMD_SET_TUNING_PARAMS    0x15
#define CMD_DEVICE_QUERY         0x16
#define CMD_GET_CONTACT_BY_KEY   0x1E
#define CMD_GET_CHANNEL          0x1F
#define CMD_SET_CHANNEL          0x20
#define CMD_GET_CUSTOM_VARS      0x28
#define CMD_SET_CUSTOM_VAR       0x29
#define CMD_GET_ADVERT_PATH      0x2A
#define CMD_SET_FLOOD_SCOPE      0x36
#define CMD_SET_AUTOADD_CONFIG   0x3A
#define CMD_GET_AUTOADD_CONFIG   0x3B

#define RESP_CODE_OK                  0x00
#define RESP_CODE_ERR                 0x01
#define RESP_CODE_CONTACTS_START      0x02
#define RESP_CODE_CONTACT             0x03
#define RESP_CODE_END_OF_CONTACTS     0x04
#define RESP_CODE_SELF_INFO           0x05
#define RESP_CODE_SENT                0x06
#define RESP_CODE_CONTACT_MSG_RECV    0x07
#define RESP_CODE_CHANNEL_MSG_RECV    0x08
#define RESP_CODE_CURR_TIME           0x09
#define RESP_CODE_NO_MORE_MESSAGES    0x0A
#define RESP_CODE_EXPORT_CONTACT      0x0B
#define RESP_CODE_BATT_AND_STORAGE    0x0C
#define RESP_CODE_DEVICE_INFO         0x0D
#define RESP_CODE_CONTACT_MSG_RECV_V3 0x10
#define RESP_CODE_CHANNEL_MSG_RECV_V3 0x11
#define RESP_CODE_CHANNEL_INFO        0x12
#define RESP_CODE_CUSTOM_VARS         0x15
#define RESP_CODE_ADVERT_PATH         0x16
#define RESP_CODE_AUTOADD_CONFIG      0x19

#define PUSH_CODE_ADVERT         0x80
#define PUSH_CODE_PATH_UPDATED   0x81
#define PUSH_CODE_SEND_CONFIRMED 0x82
#define PUSH_CODE_MSG_WAITING    0x83
#define PUSH_CODE_NEW_ADVERT     0x8A

#define ERR_CODE_UNSUPPORTED_CMD 1
#define ERR_CODE_NOT_FOUND       2
#define ERR_CODE_TABLE_FULL      3
#define ERR_CODE_BAD_STATE       4
#define ERR_CODE_FILE_IO_ERROR   5
#define ERR_CODE_ILLEGAL_ARG     6

#define FIRMWARE_VER_CODE 10
#define FIRMWARE_VERSION  "v1.14.1"
#define DEVICE_MODEL      "Highboy-MC-S3"

#define TXT_TYPE_PLAIN 0x00

#define CONTACT_FRAME_SIZE 143

#define MC_OFFLINE_QUEUE_SIZE 16
#define MC_OFFLINE_FRAME_MAX  240

#define MC_NVS_KEY_BLE_PIN "ble_pin"
#define MC_NVS_KEY_AUTOADD "autoadd"

#define MC_BLE_PIN_MIN 100000U
#define MC_BLE_PIN_MAX 999999U

#define MESHCORE_PUBKEY_PREFIX_LEN     6
#define MESHCORE_CHANNEL_SECRET_LEN    16
#define SEND_TXT_DEFAULT_TIMEOUT_MS    10000
#define BATT_STORAGE_DEFAULT_MV        4100
#define BATT_STORAGE_DEFAULT_USED_KB   8
#define BATT_STORAGE_DEFAULT_TOTAL_KB  1024

#define MC_AUTOADD_DEFAULT_CONFIG      0
#define MC_AUTOADD_DEFAULT_MAX_HOPS    3
#define MC_AUTOADD_MAX_HOPS_CLAMP      64

typedef struct {
  uint16_t len;
  uint8_t buf[MC_OFFLINE_FRAME_MAX];
} mc_queued_frame_t;

static bool s_is_initialized = false;
static uint8_t s_app_target_ver = 0;
static uint32_t s_ble_pin = 0;
static uint8_t s_autoadd_config = MC_AUTOADD_DEFAULT_CONFIG;
static uint8_t s_autoadd_max_hops = MC_AUTOADD_DEFAULT_MAX_HOPS;
static meshcore_phoneapi_outbound_cb_t s_outbound_cb = NULL;
static void *s_outbound_ctx = NULL;

static mc_queued_frame_t s_offline_queue[MC_OFFLINE_QUEUE_SIZE];
static uint8_t s_offline_queue_len = 0;

static void send_resp(const uint8_t *buf, uint16_t len);
static void send_err(uint8_t sub);
static void send_ok(void);

static uint32_t load_or_generate_ble_pin(void);

static void offline_queue_push(const uint8_t *frame, uint16_t len);
static bool offline_queue_pop(uint8_t *out, uint16_t *out_len);

static void serialize_contact(uint8_t out[CONTACT_FRAME_SIZE], const meshcore_contact_t *c);
static void deserialize_contact(meshcore_contact_t *c, const uint8_t in[], size_t in_len);
static void write_contact_resp_frame(uint8_t code, const meshcore_contact_t *c);

static void handle_device_query(const uint8_t *p, uint16_t len);
static void handle_app_start(const uint8_t *p, uint16_t len);
static void handle_batt_storage(const uint8_t *p, uint16_t len);
static void handle_sync_next_msg(const uint8_t *p, uint16_t len);
static void handle_get_device_time(const uint8_t *p, uint16_t len);
static void handle_set_device_time(const uint8_t *p, uint16_t len);
static void handle_send_self_advert(const uint8_t *p, uint16_t len);
static void handle_set_advert_name(const uint8_t *p, uint16_t len);
static void handle_set_advert_latlon(const uint8_t *p, uint16_t len);
static void handle_add_update_contact(const uint8_t *p, uint16_t len);
static void handle_get_contact_by_key(const uint8_t *p, uint16_t len);
static void handle_get_contacts(const uint8_t *p, uint16_t len);
static void handle_remove_contact(const uint8_t *p, uint16_t len);
static void handle_share_contact(const uint8_t *p, uint16_t len);
static void handle_export_contact(const uint8_t *p, uint16_t len);
static void handle_import_contact(const uint8_t *p, uint16_t len);
static void handle_reset_path(const uint8_t *p, uint16_t len);
static void handle_get_channel(const uint8_t *p, uint16_t len);
static void handle_set_channel(const uint8_t *p, uint16_t len);
static void handle_get_advert_path(const uint8_t *p, uint16_t len);
static void handle_send_channel_txt(const uint8_t *p, uint16_t len);
static void handle_send_txt_msg(const uint8_t *p, uint16_t len);
static void handle_set_radio_params(const uint8_t *p, uint16_t len);
static void handle_set_radio_tx_power(const uint8_t *p, uint16_t len);
static void handle_get_autoadd_config(const uint8_t *p, uint16_t len);
static void handle_set_autoadd_config(const uint8_t *p, uint16_t len);
static void handle_get_custom_vars(const uint8_t *p, uint16_t len);
static void handle_set_custom_var(const uint8_t *p, uint16_t len);
static void autoadd_load(void);
static void autoadd_persist(void);

esp_err_t meshcore_phoneapi_init(void) {
  if (s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  s_ble_pin = load_or_generate_ble_pin();
  autoadd_load();
  s_app_target_ver = 0;
  s_offline_queue_len = 0;
  s_outbound_cb = NULL;
  s_outbound_ctx = NULL;
  s_is_initialized = true;
  ESP_LOGI(TAG, "Initialized — PIN=%lu", (unsigned long)s_ble_pin);
  return ESP_OK;
}

void meshcore_phoneapi_set_outbound(meshcore_phoneapi_outbound_cb_t cb, void *ctx) {
  s_outbound_cb = cb;
  s_outbound_ctx = ctx;
}

uint32_t meshcore_phoneapi_get_pin(void) {
  return s_ble_pin;
}

void meshcore_phoneapi_on_disconnect(void) {
  s_app_target_ver = 0;
  s_offline_queue_len = 0;
}

void meshcore_phoneapi_on_inbound(const uint8_t *buf, uint16_t len) {
  if (buf == NULL || len == 0) {
    return;
  }

  ESP_LOGI(TAG, "CMD 0x%02X (%u bytes)", buf[0], len);

  switch (buf[0]) {
    case CMD_DEVICE_QUERY:
      handle_device_query(buf, len);
      break;
    case CMD_APP_START:
      handle_app_start(buf, len);
      break;
    case CMD_GET_BATT_AND_STORAGE:
      handle_batt_storage(buf, len);
      break;
    case CMD_SYNC_NEXT_MESSAGE:
      handle_sync_next_msg(buf, len);
      break;
    case CMD_GET_DEVICE_TIME:
      handle_get_device_time(buf, len);
      break;
    case CMD_SET_DEVICE_TIME:
      handle_set_device_time(buf, len);
      break;
    case CMD_SEND_SELF_ADVERT:
      handle_send_self_advert(buf, len);
      break;
    case CMD_SET_ADVERT_NAME:
      handle_set_advert_name(buf, len);
      break;
    case CMD_SET_ADVERT_LATLON:
      handle_set_advert_latlon(buf, len);
      break;
    case CMD_SEND_CHANNEL_TXT_MSG:
      handle_send_channel_txt(buf, len);
      break;
    case CMD_SEND_TXT_MSG:
      handle_send_txt_msg(buf, len);
      break;
    case CMD_GET_CONTACTS:
      handle_get_contacts(buf, len);
      break;
    case CMD_GET_CHANNEL:
      handle_get_channel(buf, len);
      break;
    case CMD_SET_CHANNEL:
      handle_set_channel(buf, len);
      break;
    case CMD_GET_ADVERT_PATH:
      handle_get_advert_path(buf, len);
      break;
    case CMD_ADD_UPDATE_CONTACT:
      handle_add_update_contact(buf, len);
      break;
    case CMD_GET_CONTACT_BY_KEY:
      handle_get_contact_by_key(buf, len);
      break;
    case CMD_REMOVE_CONTACT:
      handle_remove_contact(buf, len);
      break;
    case CMD_SHARE_CONTACT:
      handle_share_contact(buf, len);
      break;
    case CMD_EXPORT_CONTACT:
      handle_export_contact(buf, len);
      break;
    case CMD_IMPORT_CONTACT:
      handle_import_contact(buf, len);
      break;
    case CMD_RESET_PATH:
      handle_reset_path(buf, len);
      break;
    case CMD_SET_RADIO_PARAMS:
      handle_set_radio_params(buf, len);
      break;
    case CMD_SET_RADIO_TX_POWER:
      handle_set_radio_tx_power(buf, len);
      break;
    case CMD_GET_AUTOADD_CONFIG:
      handle_get_autoadd_config(buf, len);
      break;
    case CMD_SET_AUTOADD_CONFIG:
      handle_set_autoadd_config(buf, len);
      break;
    case CMD_GET_CUSTOM_VARS:
      handle_get_custom_vars(buf, len);
      break;
    case CMD_SET_CUSTOM_VAR:
      handle_set_custom_var(buf, len);
      break;

    case CMD_SET_TUNING_PARAMS:
    case CMD_SET_FLOOD_SCOPE:
      send_ok();
      break;

    default:
      ESP_LOGW(TAG, "Unhandled CMD 0x%02X", buf[0]);
      send_ok();
      break;
  }
}

void meshcore_phoneapi_push_new_advert(const meshcore_contact_t *c) {
  uint8_t buf[1 + CONTACT_FRAME_SIZE + 4];
  buf[0] = PUSH_CODE_NEW_ADVERT;
  serialize_contact(&buf[1], c);
  memcpy(&buf[1 + CONTACT_FRAME_SIZE], &c->lastmod, 4);
  send_resp(buf, sizeof(buf));
  ESP_LOGI(TAG, "push NEW_ADVERT name='%s' type=%u", c->name, c->type);
}

void meshcore_phoneapi_push_channel_msg(
    uint8_t channel_idx, uint8_t path_len, uint32_t timestamp, int8_t snr, const char *text) {
  uint8_t buf[240];
  uint16_t o = 0;

  if (s_app_target_ver >= 3) {
    buf[o++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    buf[o++] = (uint8_t)((int8_t)(snr * 4));
    buf[o++] = 0;
    buf[o++] = 0;
  } else {
    buf[o++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  buf[o++] = channel_idx;
  buf[o++] = path_len;
  buf[o++] = TXT_TYPE_PLAIN;
  memcpy(&buf[o], &timestamp, 4);
  o += 4;

  uint16_t tl = (uint16_t)strlen(text);
  if (o + tl >= sizeof(buf))
    tl = sizeof(buf) - o;
  memcpy(&buf[o], text, tl);
  o += tl;

  offline_queue_push(buf, o);
  uint8_t tickle = PUSH_CODE_MSG_WAITING;
  send_resp(&tickle, 1);
  ESP_LOGI(TAG,
           "queue CHANNEL_MSG %s ch=%u snr=%d text='%s'",
           s_app_target_ver >= 3 ? "V3" : "V1",
           channel_idx,
           snr,
           text);
}

void meshcore_phoneapi_push_contact_msg(const uint8_t peer_pub_key[32],
                                        uint8_t path_len,
                                        uint8_t txt_type,
                                        uint32_t timestamp,
                                        int8_t snr,
                                        const char *text) {
  uint8_t buf[240];
  uint16_t o = 0;

  if (s_app_target_ver >= 3) {
    buf[o++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    buf[o++] = (uint8_t)((int8_t)(snr * 4));
    buf[o++] = 0;
    buf[o++] = 0;
  } else {
    buf[o++] = RESP_CODE_CONTACT_MSG_RECV;
  }

  memcpy(&buf[o], peer_pub_key, 6);
  o += 6;
  buf[o++] = path_len;
  buf[o++] = txt_type;
  memcpy(&buf[o], &timestamp, 4);
  o += 4;

  uint16_t tl = (uint16_t)strlen(text);
  if (o + tl >= sizeof(buf))
    tl = sizeof(buf) - o;
  memcpy(&buf[o], text, tl);
  o += tl;

  offline_queue_push(buf, o);
  uint8_t tickle = PUSH_CODE_MSG_WAITING;
  send_resp(&tickle, 1);
  ESP_LOGI(TAG,
           "queue CONTACT_MSG %s peer=%02X%02X%02X%02X txt_type=%u text='%s'",
           s_app_target_ver >= 3 ? "V3" : "V1",
           peer_pub_key[0],
           peer_pub_key[1],
           peer_pub_key[2],
           peer_pub_key[3],
           txt_type,
           text);
}

void meshcore_phoneapi_push_send_confirmed(uint32_t ack_crc, uint8_t snr) {
  (void)snr;
  uint8_t buf[9];
  buf[0] = PUSH_CODE_SEND_CONFIRMED;
  memcpy(&buf[1], &ack_crc, 4);
  uint32_t trip_time_ms = 0;
  memcpy(&buf[5], &trip_time_ms, 4);
  send_resp(buf, sizeof(buf));
  ESP_LOGI(TAG, "push SEND_CONFIRMED CRC=0x%08lX", (unsigned long)ack_crc);
}

void meshcore_phoneapi_push_path_updated(const meshcore_contact_t *c) {
  uint8_t buf[1 + CONTACT_FRAME_SIZE + 4];
  buf[0] = PUSH_CODE_PATH_UPDATED;
  serialize_contact(&buf[1], c);
  memcpy(&buf[1 + CONTACT_FRAME_SIZE], &c->lastmod, 4);
  send_resp(buf, sizeof(buf));
}

static void send_resp(const uint8_t *buf, uint16_t len) {
  if (s_outbound_cb == NULL) {
    ESP_LOGW(TAG, "No outbound bridge, drop %u bytes", len);
    return;
  }
  s_outbound_cb(s_outbound_ctx, buf, len);
}

static void send_err(uint8_t sub) {
  uint8_t resp[2] = {RESP_CODE_ERR, sub};
  send_resp(resp, 2);
}

static void send_ok(void) {
  uint8_t resp[1] = {RESP_CODE_OK};
  send_resp(resp, 1);
}

static uint32_t load_or_generate_ble_pin(void) {
  uint32_t pin = 0;
  if (mc_nvs_get_u32(MC_NVS_KEY_BLE_PIN, &pin) == ESP_OK && pin >= MC_BLE_PIN_MIN &&
      pin <= MC_BLE_PIN_MAX) {
    return pin;
  }
  pin = MC_BLE_PIN_MIN + (esp_random() % (MC_BLE_PIN_MAX - MC_BLE_PIN_MIN + 1));
  mc_nvs_set_u32(MC_NVS_KEY_BLE_PIN, pin);
  ESP_LOGI(TAG, "BLE PIN generated: %lu", (unsigned long)pin);
  return pin;
}

static void offline_queue_push(const uint8_t *frame, uint16_t len) {
  if (len == 0 || len > MC_OFFLINE_FRAME_MAX)
    return;
  if (s_offline_queue_len >= MC_OFFLINE_QUEUE_SIZE) {
    for (int i = 0; i < MC_OFFLINE_QUEUE_SIZE - 1; i++) {
      s_offline_queue[i] = s_offline_queue[i + 1];
    }
    s_offline_queue_len = MC_OFFLINE_QUEUE_SIZE - 1;
  }
  s_offline_queue[s_offline_queue_len].len = len;
  memcpy(s_offline_queue[s_offline_queue_len].buf, frame, len);
  s_offline_queue_len++;
}

static bool offline_queue_pop(uint8_t *out, uint16_t *out_len) {
  if (s_offline_queue_len == 0)
    return false;
  *out_len = s_offline_queue[0].len;
  memcpy(out, s_offline_queue[0].buf, *out_len);
  for (int i = 0; i < s_offline_queue_len - 1; i++) {
    s_offline_queue[i] = s_offline_queue[i + 1];
  }
  s_offline_queue_len--;
  return true;
}

static void serialize_contact(uint8_t out[CONTACT_FRAME_SIZE], const meshcore_contact_t *c) {
  uint16_t o = 0;
  memcpy(&out[o], c->pub_key, 32);
  o += 32;
  out[o++] = c->type;
  out[o++] = c->flags;
  out[o++] = c->out_path_len;
  memcpy(&out[o], c->out_path, MESHCORE_MAX_PATH);
  o += MESHCORE_MAX_PATH;
  memset(&out[o], 0, MESHCORE_NAME_MAX);
  strncpy((char *)&out[o], c->name, MESHCORE_NAME_MAX);
  o += MESHCORE_NAME_MAX;
  memcpy(&out[o], &c->last_advert, 4);
  o += 4;
  memcpy(&out[o], &c->gps_lat, 4);
  o += 4;
  memcpy(&out[o], &c->gps_lon, 4);
  o += 4;
}

static void deserialize_contact(meshcore_contact_t *c, const uint8_t in[], size_t in_len) {
  memset(c, 0, sizeof(*c));
  c->is_used = true;
  if (in_len < CONTACT_FRAME_SIZE)
    return;
  uint16_t o = 0;
  memcpy(c->pub_key, &in[o], 32);
  o += 32;
  c->type = in[o++];
  c->flags = in[o++];
  c->out_path_len = in[o++];
  memcpy(c->out_path, &in[o], MESHCORE_MAX_PATH);
  o += MESHCORE_MAX_PATH;
  char tmp[MESHCORE_NAME_MAX + 1] = {0};
  memcpy(tmp, &in[o], MESHCORE_NAME_MAX);
  strncpy(c->name, tmp, MESHCORE_NAME_MAX - 1);
  c->name[MESHCORE_NAME_MAX - 1] = 0;
  o += MESHCORE_NAME_MAX;
  memcpy(&c->last_advert, &in[o], 4);
  o += 4;
  memcpy(&c->gps_lat, &in[o], 4);
  o += 4;
  memcpy(&c->gps_lon, &in[o], 4);
  o += 4;
  if (in_len >= CONTACT_FRAME_SIZE + 4) {
    memcpy(&c->lastmod, &in[o], 4);
  }
}

static void write_contact_resp_frame(uint8_t code, const meshcore_contact_t *c) {
  uint8_t buf[1 + CONTACT_FRAME_SIZE + 4];
  buf[0] = code;
  serialize_contact(&buf[1], c);
  memcpy(&buf[1 + CONTACT_FRAME_SIZE], &c->lastmod, 4);
  send_resp(buf, sizeof(buf));
}

static void handle_device_query(const uint8_t *p, uint16_t len) {
  if (len >= 2) {
    s_app_target_ver = p[1];
    ESP_LOGI(TAG, "app_target_ver = %u", s_app_target_ver);
  }
  uint8_t resp[80];
  uint16_t o = 0;
  resp[o++] = RESP_CODE_DEVICE_INFO;
  resp[o++] = FIRMWARE_VER_CODE;
  resp[o++] = MESHCORE_MAX_CONTACTS / 2;
  resp[o++] = MESHCORE_MAX_CHANNELS;

  resp[o++] = (uint8_t)(s_ble_pin & 0xFF);
  resp[o++] = (uint8_t)((s_ble_pin >> 8) & 0xFF);
  resp[o++] = (uint8_t)((s_ble_pin >> 16) & 0xFF);
  resp[o++] = (uint8_t)((s_ble_pin >> 24) & 0xFF);

  const char *bd = __DATE__;
  uint8_t bl = (uint8_t)strlen(bd);
  if (bl > 12)
    bl = 12;
  memcpy(&resp[o], bd, bl);
  o += bl;
  while (bl++ < 12) {
    resp[o++] = 0;
  }

  const char *mo = DEVICE_MODEL;
  uint8_t ml = (uint8_t)strlen(mo);
  if (ml > 40)
    ml = 40;
  memcpy(&resp[o], mo, ml);
  o += ml;
  while (ml++ < 40) {
    resp[o++] = 0;
  }

  const char *ve = FIRMWARE_VERSION;
  uint8_t vl = (uint8_t)strlen(ve);
  if (vl > 20)
    vl = 20;
  memcpy(&resp[o], ve, vl);
  o += vl;
  while (vl++ < 20) {
    resp[o++] = 0;
  }

  resp[o++] = 0;
  resp[o++] = 0;

  ESP_LOGI(TAG, "-> DEVICE_INFO (%u bytes, target_ver=%u)", o, s_app_target_ver);
  send_resp(resp, o);
}

static void handle_app_start(const uint8_t *p, uint16_t len) {
  const meshcore_radio_prefs_t *rp = meshcore_get_radio_prefs();

  uint8_t resp[128];
  uint16_t o = 0;
  resp[o++] = RESP_CODE_SELF_INFO;
  resp[o++] = meshcore_get_adv_type();
  resp[o++] = (uint8_t)rp->tx_power_dbm;
  resp[o++] = 22;

  uint8_t pub[32] = {0};
  meshcore_get_pub_key(pub);
  memcpy(&resp[o], pub, 32);
  o += 32;

  int32_t lat = 0, lon = 0;
  bool is_has_latlon = false;
  meshcore_get_advert_latlon(&lat, &lon, &is_has_latlon);
  memcpy(&resp[o], &lat, 4);
  o += 4;
  memcpy(&resp[o], &lon, 4);
  o += 4;

  resp[o++] = 0;
  resp[o++] = 0;
  resp[o++] = 0;
  resp[o++] = 0;

  uint32_t freq_khz = rp->freq_hz / 1000;
  uint32_t bw_khz = rp->bw_hz / 1000;
  memcpy(&resp[o], &freq_khz, 4);
  o += 4;
  memcpy(&resp[o], &bw_khz, 4);
  o += 4;
  resp[o++] = rp->sf;
  resp[o++] = rp->cr;

  char nm[MESHCORE_NAME_MAX] = {0};
  meshcore_get_name(nm, sizeof(nm));
  uint8_t nl = (uint8_t)strlen(nm);
  if (o + nl >= sizeof(resp))
    nl = (uint8_t)(sizeof(resp) - o - 1);
  memcpy(&resp[o], nm, nl);
  o += nl;

  ESP_LOGI(TAG, "-> SELF_INFO (%u bytes, name='%s')", o, nm);
  send_resp(resp, o);
}

static void handle_batt_storage(const uint8_t *p, uint16_t len) {
  (void)p;
  (void)len;
  uint8_t resp[11];
  uint16_t o = 0;
  resp[o++] = RESP_CODE_BATT_AND_STORAGE;
  uint16_t batt_mv = BATT_STORAGE_DEFAULT_MV;
  memcpy(&resp[o], &batt_mv, 2);
  o += 2;
  uint32_t used_kb = BATT_STORAGE_DEFAULT_USED_KB;
  uint32_t total_kb = BATT_STORAGE_DEFAULT_TOTAL_KB;
  memcpy(&resp[o], &used_kb, 4);
  o += 4;
  memcpy(&resp[o], &total_kb, 4);
  o += 4;
  send_resp(resp, o);
}

static void handle_sync_next_msg(const uint8_t *p, uint16_t len) {
  uint8_t frame[MC_OFFLINE_FRAME_MAX];
  uint16_t flen = 0;
  if (offline_queue_pop(frame, &flen)) {
    ESP_LOGI(TAG, "-> SYNC msg (qlen=%u, %u bytes)", s_offline_queue_len, flen);
    send_resp(frame, flen);
    return;
  }
  uint8_t resp[1] = {RESP_CODE_NO_MORE_MESSAGES};
  send_resp(resp, 1);
}

static void handle_get_device_time(const uint8_t *p, uint16_t len) {
  uint32_t ts = meshcore_get_unix_time();
  uint8_t resp[5];
  resp[0] = RESP_CODE_CURR_TIME;
  memcpy(&resp[1], &ts, 4);
  send_resp(resp, sizeof(resp));
}

static void handle_set_device_time(const uint8_t *p, uint16_t len) {
  if (len >= 5) {
    uint32_t ts;
    memcpy(&ts, &p[1], 4);
    meshcore_set_unix_time(ts);
  }
  send_ok();
}

static void handle_send_self_advert(const uint8_t *p, uint16_t len) {
  meshcore_send_advert();
  send_ok();
}

static void handle_set_advert_name(const uint8_t *p, uint16_t len) {
  if (len < 2) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  char name[MESHCORE_NAME_MAX] = {0};
  uint16_t nl = len - 1;
  if (nl > MESHCORE_NAME_MAX - 1)
    nl = MESHCORE_NAME_MAX - 1;
  memcpy(name, &p[1], nl);
  name[nl] = 0;
  meshcore_set_node_name(name);
  send_ok();
}

static void handle_set_advert_latlon(const uint8_t *p, uint16_t len) {
  if (len < 1 + 8) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  int32_t lat, lon;
  memcpy(&lat, &p[1], 4);
  memcpy(&lon, &p[5], 4);
  bool has = (lat != 0 || lon != 0);
  meshcore_set_advert_latlon(lat, lon, has);
  send_ok();
}

static void handle_add_update_contact(const uint8_t *p, uint16_t len) {
  if (len < 1 + CONTACT_FRAME_SIZE) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  meshcore_contact_t c;
  deserialize_contact(&c, &p[1], len - 1);
  if (!meshcore_contact_upsert(&c)) {
    send_err(ERR_CODE_TABLE_FULL);
    return;
  }
  ESP_LOGI(TAG,
           "Contact ADD/UPDATE pubkey=%02X%02X%02X%02X type=%u",
           c.pub_key[0],
           c.pub_key[1],
           c.pub_key[2],
           c.pub_key[3],
           c.type);
  send_ok();
}

static void handle_get_contact_by_key(const uint8_t *p, uint16_t len) {
  if (len < 1 + 32) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  const meshcore_contact_t *c = meshcore_contact_find(&p[1]);
  if (c == NULL) {
    send_err(ERR_CODE_NOT_FOUND);
    return;
  }
  write_contact_resp_frame(RESP_CODE_CONTACT, c);
}

static void handle_get_contacts(const uint8_t *p, uint16_t len) {
  uint32_t since = 0;
  if (len >= 5)
    memcpy(&since, &p[1], 4);

  uint32_t count = 0;
  const meshcore_contact_t *arr = meshcore_contacts_array();
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (arr[i].is_used && arr[i].lastmod >= since)
      count++;
  }

  uint8_t start[5];
  start[0] = RESP_CODE_CONTACTS_START;
  memcpy(&start[1], &count, 4);
  send_resp(start, 5);

  uint32_t max_lastmod = 0;
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (arr[i].is_used && arr[i].lastmod >= since) {
      write_contact_resp_frame(RESP_CODE_CONTACT, &arr[i]);
      if (arr[i].lastmod > max_lastmod)
        max_lastmod = arr[i].lastmod;
    }
  }

  uint8_t end[5] = {RESP_CODE_END_OF_CONTACTS, 0, 0, 0, 0};
  memcpy(&end[1], &max_lastmod, 4);
  send_resp(end, 5);
  ESP_LOGI(TAG, "-> CONTACTS (%lu entries since=%lu)", (unsigned long)count, (unsigned long)since);
}

static void handle_remove_contact(const uint8_t *p, uint16_t len) {
  if (len < 1 + 32) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  if (!meshcore_contact_remove(&p[1])) {
    send_err(ERR_CODE_NOT_FOUND);
    return;
  }
  send_ok();
}

static void handle_share_contact(const uint8_t *p, uint16_t len) {
  meshcore_send_advert();
  send_ok();
}

static void handle_export_contact(const uint8_t *p, uint16_t len) {
  if (len < 1 + 32) {
    uint8_t resp[1 + MESHCORE_MAX_RAW];
    resp[0] = RESP_CODE_EXPORT_CONTACT;
    int32_t lat = 0, lon = 0;
    bool has_latlon = false;
    mc_get_advert_latlon_internal(&lat, &lon, &has_latlon);
    uint16_t n = meshcore_packet_build_advert(mc_get_identity(),
                                                lat, lon, has_latlon,
                                                meshcore_get_unix_time(),
                                                &resp[1], sizeof(resp) - 1);
    if (n == 0) {
      send_err(ERR_CODE_TABLE_FULL);
      return;
    }
    send_resp(resp, 1 + n);
    return;
  }
  const meshcore_contact_t *c = meshcore_contact_find(&p[1]);
  if (c == NULL) {
    send_err(ERR_CODE_NOT_FOUND);
    return;
  }
  uint8_t resp[1 + 32];
  resp[0] = RESP_CODE_EXPORT_CONTACT;
  memcpy(&resp[1], c->pub_key, 32);
  send_resp(resp, sizeof(resp));
}

static void handle_import_contact(const uint8_t *p, uint16_t len) {
  if (len < 1 + 32 + 2) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  meshcore_contact_t c;
  memset(&c, 0, sizeof(c));
  c.is_used = true;
  memcpy(c.pub_key, &p[1], 32);
  c.type = p[33];
  c.flags = p[34];
  c.out_path_len = MESHCORE_OUT_PATH_UNKNOWN;
  c.lastmod = meshcore_get_unix_time();
  if (!meshcore_contact_upsert(&c)) {
    send_err(ERR_CODE_TABLE_FULL);
    return;
  }
  send_ok();
}

static void handle_reset_path(const uint8_t *p, uint16_t len) {
  if (len < 1 + 32) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  if (!meshcore_contact_reset_path(&p[1])) {
    send_err(ERR_CODE_NOT_FOUND);
    return;
  }
  send_ok();
}

static void handle_get_channel(const uint8_t *p, uint16_t len) {
  uint8_t idx = (len >= 2) ? p[1] : 0;
  if (idx >= MESHCORE_MAX_CHANNELS) {
    send_err(ERR_CODE_NOT_FOUND);
    return;
  }
  const meshcore_channel_t *ch = meshcore_channel_get(idx);
  uint8_t resp[2 + MESHCORE_NAME_MAX + MESHCORE_CHANNEL_SECRET_LEN];
  uint16_t o = 0;
  resp[o++] = RESP_CODE_CHANNEL_INFO;
  resp[o++] = idx;
  memset(&resp[o], 0, MESHCORE_NAME_MAX);
  if (ch != NULL) {
    strncpy((char *)&resp[o], ch->name, MESHCORE_NAME_MAX);
  }
  o += MESHCORE_NAME_MAX;
  if (ch != NULL) {
    memcpy(&resp[o], ch->secret, MESHCORE_CHANNEL_SECRET_LEN);
  } else {
    memset(&resp[o], 0, MESHCORE_CHANNEL_SECRET_LEN);
  }
  o += MESHCORE_CHANNEL_SECRET_LEN;
  send_resp(resp, o);
}

static void handle_set_channel(const uint8_t *p, uint16_t len) {
  if (len < 2 + MESHCORE_NAME_MAX + 16) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  uint8_t idx = p[1];
  char name[MESHCORE_NAME_MAX + 1] = {0};
  memcpy(name, &p[2], MESHCORE_NAME_MAX);
  if (!meshcore_channel_set(idx, name, &p[2 + MESHCORE_NAME_MAX])) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  send_ok();
}

static void handle_get_advert_path(const uint8_t *p, uint16_t len) {
  if (len < 2 + 32) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  const meshcore_contact_t *c = meshcore_contact_find(&p[2]);
  uint32_t ts = meshcore_get_unix_time();
  uint8_t resp[6 + MESHCORE_MAX_PATH];
  uint16_t o = 0;
  resp[o++] = RESP_CODE_ADVERT_PATH;
  memcpy(&resp[o], &ts, 4);
  o += 4;
  if (c == NULL || c->out_path_len == MESHCORE_OUT_PATH_UNKNOWN) {
    resp[o++] = 0;
  } else {
    resp[o++] = c->out_path_len;
    memcpy(&resp[o], c->out_path, c->out_path_len);
    o += c->out_path_len;
  }
  send_resp(resp, o);
}

static void handle_send_channel_txt(const uint8_t *p, uint16_t len) {
  if (len < 1 + 1 + 1 + 4) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  uint8_t channel_idx = p[2];
  uint16_t text_off = 1 + 1 + 1 + 4;
  uint16_t text_len = len - text_off;

  char text[MESHCORE_TEXT_MAX + 1];
  if (text_len > sizeof(text) - 1) {
    text_len = sizeof(text) - 1;
  }
  memcpy(text, &p[text_off], text_len);
  text[text_len] = 0;

  esp_err_t ret = meshcore_send_grp_txt(channel_idx, text);
  if (ret != ESP_OK) {
    send_err(ERR_CODE_NOT_FOUND);
    return;
  }
  send_ok();
}

static void handle_send_txt_msg(const uint8_t *p, uint16_t len) {
  if (len < 1 + 1 + 1 + 4 + MESHCORE_PUBKEY_PREFIX_LEN) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  uint8_t txt_type = p[1];
  uint8_t attempt = p[2];
  uint32_t ts;
  memcpy(&ts, &p[3], 4);
  const uint8_t *prefix = &p[7];
  uint16_t text_off = 1 + 1 + 1 + 4 + MESHCORE_PUBKEY_PREFIX_LEN;
  uint16_t text_len = len - text_off;
  char text[MESHCORE_TEXT_MAX + 1];
  if (text_len > sizeof(text) - 1) {
    text_len = sizeof(text) - 1;
  }
  memcpy(text, &p[text_off], text_len);
  text[text_len] = 0;
  (void)txt_type;

  const meshcore_contact_t *arr = meshcore_contacts_array();
  const meshcore_contact_t *contact = NULL;
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (arr[i].is_used && memcmp(arr[i].pub_key, prefix, MESHCORE_PUBKEY_PREFIX_LEN) == 0) {
      contact = &arr[i];
      break;
    }
  }
  if (contact == NULL) {
    ESP_LOGW(TAG,
             "DM TX: contact with prefix %02X%02X%02X%02X%02X%02X not found",
             prefix[0], prefix[1], prefix[2], prefix[3], prefix[4], prefix[5]);
    send_err(ERR_CODE_NOT_FOUND);
    return;
  }

  uint32_t expected_ack = 0;
  esp_err_t ret = meshcore_send_direct_msg(contact->pub_key, text, ts, attempt, &expected_ack);
  if (ret != ESP_OK) {
    send_err(ERR_CODE_BAD_STATE);
    return;
  }

  uint8_t resp[1 + 1 + 4 + 4];
  uint16_t o = 0;
  resp[o++] = RESP_CODE_SENT;
  resp[o++] = 1;
  memcpy(&resp[o], &expected_ack, 4);
  o += 4;
  uint32_t timeout_ms = SEND_TXT_DEFAULT_TIMEOUT_MS;
  memcpy(&resp[o], &timeout_ms, 4);
  o += 4;
  send_resp(resp, o);
  ESP_LOGI(TAG,
           "DM TX queued (peer=%02X%02X%02X%02X tag=0x%08lX)",
           contact->pub_key[0],
           contact->pub_key[1],
           contact->pub_key[2],
           contact->pub_key[3],
           (unsigned long)expected_ack);
}

static void handle_set_radio_params(const uint8_t *p, uint16_t len) {
  if (len < 1 + 4 + 4 + 1 + 1) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  uint32_t freq_khz, bw_khz;
  memcpy(&freq_khz, &p[1], 4);
  memcpy(&bw_khz, &p[5], 4);
  uint8_t sf = p[9];
  uint8_t cr = p[10];

  if (meshcore_set_radio_params(
          freq_khz * 1000UL, bw_khz * 1000UL, sf, cr, meshcore_get_radio_prefs()->tx_power_dbm) !=
      ESP_OK) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  send_ok();
}

static void handle_set_radio_tx_power(const uint8_t *p, uint16_t len) {
  if (len < 2) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  int8_t power = (int8_t)p[1];
  const meshcore_radio_prefs_t *rp = meshcore_get_radio_prefs();
  if (meshcore_set_radio_params(rp->freq_hz, rp->bw_hz, rp->sf, rp->cr, power) != ESP_OK) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  send_ok();
}

static void handle_get_autoadd_config(const uint8_t *p, uint16_t len) {
  (void)p;
  (void)len;
  uint8_t resp[3];
  resp[0] = RESP_CODE_AUTOADD_CONFIG;
  resp[1] = s_autoadd_config;
  resp[2] = s_autoadd_max_hops;
  send_resp(resp, sizeof(resp));
}

static void handle_set_autoadd_config(const uint8_t *p, uint16_t len) {
  if (len < 2) {
    send_err(ERR_CODE_ILLEGAL_ARG);
    return;
  }
  s_autoadd_config = p[1];
  if (len >= 3) {
    uint8_t hops = p[2];
    if (hops > MC_AUTOADD_MAX_HOPS_CLAMP) {
      hops = MC_AUTOADD_MAX_HOPS_CLAMP;
    }
    s_autoadd_max_hops = hops;
  }
  autoadd_persist();
  send_ok();
}

static void handle_get_custom_vars(const uint8_t *p, uint16_t len) {
  (void)p;
  (void)len;
  uint8_t resp[1] = {RESP_CODE_CUSTOM_VARS};
  send_resp(resp, sizeof(resp));
}

static void handle_set_custom_var(const uint8_t *p, uint16_t len) {
  (void)p;
  (void)len;
  send_err(ERR_CODE_ILLEGAL_ARG);
}

static void autoadd_load(void) {
  uint32_t packed = 0;
  if (mc_nvs_get_u32(MC_NVS_KEY_AUTOADD, &packed) == ESP_OK) {
    s_autoadd_config = (uint8_t)(packed & 0xFF);
    s_autoadd_max_hops = (uint8_t)((packed >> 8) & 0xFF);
    if (s_autoadd_max_hops > MC_AUTOADD_MAX_HOPS_CLAMP) {
      s_autoadd_max_hops = MC_AUTOADD_MAX_HOPS_CLAMP;
    }
  }
}

static void autoadd_persist(void) {
  uint32_t packed = (uint32_t)s_autoadd_config | ((uint32_t)s_autoadd_max_hops << 8);
  mc_nvs_set_u32(MC_NVS_KEY_AUTOADD, packed);
}

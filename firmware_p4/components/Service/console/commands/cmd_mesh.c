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

#include "console_service.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "lora_session.h"
#include "meshcore.h"
#include "meshtastic_nodedb.h"
#include "sx1262_regs.h"

#define MESH_TEXT_MAX   160
#define MESH_KEY_MAX    32
#define MESH_START_ARGS 9

static const char *TAG = "MESH_TEST";
/* The console serializes commands. This token never adopts a UI session. */
static bool s_owned;
static lora_proto_t s_proto;
static uint32_t s_cursor;

static int nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static int decode(const char *hex, uint8_t *out, size_t capacity) {
  size_t n = strlen(hex);
  if (n == 0 || n % 2 != 0 || n / 2 > capacity)
    return -1;
  for (size_t i = 0; i < n / 2; ++i) {
    int a = nibble(hex[i * 2]), b = nibble(hex[i * 2 + 1]);
    if (a < 0 || b < 0)
      return -1;
    out[i] = (uint8_t)((a << 4) | b);
  }
  return (int)(n / 2);
}

static void encode(const void *data, size_t length, char *out) {
  static const char HEX[] = "0123456789abcdef";
  const uint8_t *bytes = data;
  for (size_t i = 0; i < length; ++i) {
    out[i * 2] = HEX[bytes[i] >> 4];
    out[i * 2 + 1] = HEX[bytes[i] & 15];
  }
  out[length * 2] = '\0';
}

static esp_err_t start(int argc, char **argv) {
  if (argc != MESH_START_ARGS || strcmp(argv[8], "ARMED") != 0 || s_owned)
    return ESP_ERR_INVALID_ARG;
  lora_proto_t proto = strcmp(argv[2], "mt") == 0   ? LORA_PROTO_MESHTASTIC
                       : strcmp(argv[2], "mc") == 0 ? LORA_PROTO_MESHCORE
                                                    : LORA_PROTO_NONE;
  long v[5];
  for (size_t i = 0; i < 5; ++i) {
    char *end;
    errno = 0;
    v[i] = strtol(argv[i + 3], &end, 10);
    if (errno != 0 || end == argv[i + 3] || *end != '\0')
      return ESP_ERR_INVALID_ARG;
  }
  /* Validate wide values before narrowing to the driver's byte fields. */
  if (v[0] < 863000000 || v[0] > 928000000 || v[1] < -9 || v[1] > 14 || v[2] < 7 || v[2] > 12 ||
      (v[3] != 125 && v[3] != 250 && v[3] != 500) || v[4] < 1 || v[4] > 4)
    return ESP_ERR_INVALID_ARG;
  sx1262_config_t radio = {.frequency_hz = v[0],
                           .tx_power_dbm = v[1],
                           .sf = v[2],
                           .bw = v[3] == 125   ? SX1262_LORA_BW_125
                                 : v[3] == 250 ? SX1262_LORA_BW_250
                                               : SX1262_LORA_BW_500,
                           .cr = v[4]};
  s_proto = proto;
  esp_err_t error = lora_session_start_radio_owned(proto, &s_owned, &radio, &s_owned);
  if (!s_owned)
    s_proto = LORA_PROTO_NONE;
  return error;
}

static int command(int argc, char **argv) {
  esp_err_t error = ESP_ERR_INVALID_ARG;
  if (argc >= 2 && strcmp(argv[1], "start") == 0) {
    error = start(argc, argv);
  } else if (argc == 2 && strcmp(argv[1], "stop") == 0) {
    error = s_owned ? lora_session_stop_owned(s_proto, &s_owned) : ESP_OK;
    if (error == ESP_OK) {
      s_owned = false;
      s_proto = LORA_PROTO_NONE;
    }
  } else if (argc == 2 && strcmp(argv[1], "status") == 0) {
    ESP_LOGI(TAG,
             "STATUS proto=%u owned=%u heap=%u largest=%u",
             lora_session_active(),
             s_owned,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    error = ESP_OK;
  } else if (argc == 2 && strcmp(argv[1], "messages") == 0) {
    lora_msg_t message;
    char text[MESH_TEXT_MAX * 2 + 1], who[sizeof(message.who) * 2 + 1];
    while (lora_session_msg_since(&s_cursor, &message, 1) != 0) {
      encode(message.text, strnlen(message.text, sizeof(message.text)), text);
      encode(message.who, strnlen(message.who, sizeof(message.who)), who);
      ESP_LOGI(TAG,
               "MESSAGE seq=%lu outgoing=%u who=%s text=%s",
               (unsigned long)s_cursor,
               message.outgoing,
               who,
               text);
    }
    error = ESP_OK;
  } else if (argc == 2 && strcmp(argv[1], "peers") == 0 && s_owned) {
    uint16_t count = 0;
    if (s_proto == LORA_PROTO_MESHTASTIC) {
      mt_node_entry_t node;
      for (uint16_t i = 0; mt_nodedb_snapshot(i, &node, &count) == ESP_OK && i < count; ++i)
        ESP_LOGI(
            TAG, "PEER id=%08lx rssi=%d snr=%.2f", (unsigned long)node.num, node.rssi, node.snr);
    } else {
      meshcore_contact_t node;
      char key[MESH_KEY_MAX * 2 + 1];
      for (uint16_t i = 0; meshcore_contacts_snapshot(i, &node, &count) == ESP_OK && i < count;
           ++i) {
        encode(node.pub_key, sizeof(node.pub_key), key);
        ESP_LOGI(TAG, "PEER key=%s", key);
      }
    }
    error = ESP_OK;
  } else if (argc == 2 && strcmp(argv[1], "advert") == 0 && s_owned) {
    error = lora_session_advertise();
  } else if (s_owned && ((argc == 3 && strcmp(argv[1], "send") == 0) ||
                         (argc == 4 && strcmp(argv[1], "dm") == 0))) {
    uint8_t text[MESH_TEXT_MAX], peer[MESH_KEY_MAX];
    int n = decode(argv[argc - 1], text, sizeof(text) - 1);
    if (n > 0 && memchr(text, 0, n) == NULL) {
      text[n] = 0;
      if (argc == 3)
        error = lora_session_send_text((const char *)text);
      else {
        int key_len = decode(argv[2], peer, sizeof(peer));
        if (key_len > 0)
          error = lora_session_send_direct((const char *)text, peer, key_len);
      }
    }
  }
  ESP_LOGI(TAG, "RESULT %s", esp_err_to_name(error));
  return error == ESP_OK ? 0 : 1;
}

void register_mesh_commands(void) {
  const esp_console_cmd_t cmd = {
      .command = "mesh",
      .help = "Native mesh lab: start mt|mc HZ DBM SF BW_KHZ CR ARMED; "
              "stop/status/messages/peers/advert; send HEX; dm PEER_HEX TEXT_HEX",
      .hint = NULL,
      .func = command};
  esp_err_t error = esp_console_cmd_register(&cmd);
  if (error != ESP_OK)
    ESP_LOGE(TAG, "register: %s", esp_err_to_name(error));
}

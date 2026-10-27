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

#include "meshtastic_mqtt.h"

#include "firmware_crypto.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lora_session.h"
#include "meshtastic_channels.h"
#include "meshtastic_mqtt_codec.h"
#include "meshtastic_regions.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "sys_prio.h"

#define MQTT_WORKER_STACK            8192
#define MQTT_POLL_MS                 500
#define MQTT_SPI_TIMEOUT_MS          10000
#define MQTT_TX_SLOTS                4
#define MQTT_SEEN_SLOTS              16
#define MQTT_SEQUENCE_SIZE           4
#define MQTT_HALF_SIZE               2
#define MQTT_BYTE_BITS               8
#define MQTT_HALF_BITS               16
#define MQTT_RADIO_FLAGS_OFFSET      12
#define MQTT_RADIO_CHANNEL_OFFSET    13
#define MQTT_RADIO_VIA_MQTT          0x10
#define MQTT_AES_BITS                128
#define MQTT_AES_BLOCK               16
#define MQTT_NONCE_NODE_OFFSET       8
#define MQTT_NODE_MAC_OFFSET         2
#define MQTT_NODE_MAC_BYTES          4
#define MQTT_STATUS_RECEIVED_OFFSET  1
#define MQTT_STATUS_PUBLISHED_OFFSET 5
#define MQTT_STATUS_DROPPED_OFFSET   9
#define MQTT_TLS_SCHEME              "mqtts://"
#define MQTT_PLAIN_SCHEME            "mqtt://"

typedef struct {
  char uri[SPI_MQTT_URI_MAX];
  char username[SPI_MQTT_CREDENTIAL_MAX];
  char password[SPI_MQTT_CREDENTIAL_MAX];
  char topic[SPI_MQTT_TOPIC_MAX];
  char channel[MT_MQTT_NAME_MAX];
  char gateway[MT_MQTT_NAME_MAX];
  uint8_t key[MT_PSK_SIZE];
  uint8_t channel_hash;
  uint32_t node;
} mt_mqtt_config_t;

typedef struct {
  uint16_t length;
  uint8_t data[SPI_MQTT_PACKET_MAX];
} mt_mqtt_queued_t;

typedef struct {
  uint32_t from, id;
} mt_mqtt_seen_t;

static const char *TAG = "MT_MQTT";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static bool s_is_creating;
static mt_mqtt_config_t s_config;
static mt_mqtt_snapshot_t s_snapshot;
EXT_RAM_BSS_ATTR static mt_mqtt_queued_t s_queue[MQTT_TX_SLOTS];
static uint8_t s_head, s_count;
static uint32_t s_local_dropped;
static mt_mqtt_seen_t s_seen[MQTT_SEEN_SLOTS];
static uint8_t s_seen_index;
static mt_mqtt_seen_t s_uplink_seen[MQTT_SEEN_SLOTS];
static uint8_t s_uplink_index;

static void worker(void *unused);
static esp_err_t
command(uint16_t id, const uint8_t *payload, uint8_t length, mt_mqtt_queued_t *out);
static esp_err_t set_field(uint8_t field, const char *value);
static esp_err_t connect_client(const mt_mqtt_config_t *config);
static esp_err_t publish_packet(const mt_mqtt_queued_t *packet, uint32_t sequence);
static esp_err_t receive_packet(mt_mqtt_queued_t *packet);
static void accept_message(const mt_mqtt_config_t *config, const mt_mqtt_queued_t *packet);
static void set_error(esp_err_t error);
static uint32_t read_u32(const uint8_t *data);
static uint16_t read_u16(const uint8_t *data);
static void write_u32(uint8_t *out, uint32_t value);
static void write_u16(uint8_t *out, uint16_t value);

esp_err_t meshtastic_mqtt_init(const char *broker_uri, const char *username, const char *password) {
  if (broker_uri == NULL || username == NULL || password == NULL ||
      strlen(broker_uri) >= SPI_MQTT_URI_MAX || strlen(username) >= SPI_MQTT_CREDENTIAL_MAX ||
      strlen(password) >= SPI_MQTT_CREDENTIAL_MAX)
    return ESP_ERR_INVALID_ARG;
  bool is_tls = strncmp(broker_uri, MQTT_TLS_SCHEME, sizeof(MQTT_TLS_SCHEME) - 1) == 0;
  bool is_plain = strncmp(broker_uri, MQTT_PLAIN_SCHEME, sizeof(MQTT_PLAIN_SCHEME) - 1) == 0;
  size_t scheme_length = is_tls ? sizeof(MQTT_TLS_SCHEME) - 1 : sizeof(MQTT_PLAIN_SCHEME) - 1;
  if ((!is_tls && !is_plain) || strlen(broker_uri) <= scheme_length ||
      strchr(broker_uri, '@') != NULL)
    return ESP_ERR_INVALID_ARG;
  if (lora_session_active() != LORA_PROTO_MESHTASTIC)
    return ESP_ERR_INVALID_STATE;
  const mt_channel_t *primary = NULL;
  for (uint8_t index = 0; index < MT_MAX_CHANNELS; ++index) {
    const mt_channel_t *channel = mt_channel_get(index);
    if (channel != NULL && channel->is_used && channel->role == MT_CH_PRIMARY) {
      primary = channel;
      break;
    }
  }
  const mt_region_info_t *region = mt_region_info(mt_region_current());
  if (primary == NULL || region == NULL || primary->name[0] == '\0' ||
      strpbrk(primary->name, "/+#") != NULL)
    return ESP_ERR_INVALID_STATE;
  mt_mqtt_config_t config = {0};
  uint8_t mac[6];
  esp_err_t error = esp_efuse_mac_get_default(mac);
  if (error != ESP_OK)
    return error;
  memcpy(config.uri, broker_uri, strlen(broker_uri) + 1);
  memcpy(config.username, username, strlen(username) + 1);
  memcpy(config.password, password, strlen(password) + 1);
  memcpy(config.channel, primary->name, sizeof(config.channel));
  config.channel[sizeof(config.channel) - 1] = '\0';
  config.channel_hash = primary->hash;
  memcpy(config.key, primary->psk, sizeof(config.key));
  for (uint32_t index = 0; index < MQTT_NODE_MAC_BYTES; ++index)
    config.node = (config.node << MQTT_BYTE_BITS) | mac[MQTT_NODE_MAC_OFFSET + index];
  snprintf(config.gateway, sizeof(config.gateway), "!%08lx", (unsigned long)config.node);
  int size =
      snprintf(config.topic, sizeof(config.topic), "msh/%s/2/e/%s/", region->name, config.channel);
  if (size < 0 || size + (int)strlen(config.gateway) >= (int)sizeof(config.topic))
    return ESP_ERR_INVALID_SIZE;
  portENTER_CRITICAL(&s_lock);
  bool can_start = !s_snapshot.is_requested && !s_is_creating &&
                   (s_snapshot.state == MT_MQTT_STOPPED || s_task == NULL);
  bool needs_task = s_task == NULL;
  if (can_start) {
    s_config = config;
    s_snapshot = (mt_mqtt_snapshot_t){.state = MT_MQTT_CONNECTING, .is_requested = true};
    s_head = s_count = 0;
    s_local_dropped = 0;
    s_is_creating = needs_task;
  }
  TaskHandle_t task = s_task;
  portEXIT_CRITICAL(&s_lock);
  if (!can_start)
    return ESP_ERR_INVALID_STATE;
  if (needs_task) {
    TaskHandle_t created = NULL;
    BaseType_t result = xTaskCreatePinnedToCore(worker,
                                                "mqtt_bridge",
                                                MQTT_WORKER_STACK,
                                                NULL,
                                                SYS_PRIO_SERVICE_LO,
                                                &created,
                                                SYS_CORE_RADIO);
    portENTER_CRITICAL(&s_lock);
    s_is_creating = false;
    if (result == pdPASS) {
      s_task = created;
      task = created;
    } else {
      s_snapshot.is_requested = false;
      s_snapshot.state = MT_MQTT_ERROR;
      s_snapshot.error = ESP_ERR_NO_MEM;
    }
    portEXIT_CRITICAL(&s_lock);
    if (result != pdPASS) {
      ESP_LOGE(TAG, "Cannot create MQTT bridge worker");
      return ESP_ERR_NO_MEM;
    }
  }
  xTaskNotifyGive(task);
  return ESP_OK;
}

void meshtastic_mqtt_stop(void) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot.is_requested = false;
  if (s_task != NULL)
    s_snapshot.state = MT_MQTT_STOPPING;
  TaskHandle_t task = s_task;
  portEXIT_CRITICAL(&s_lock);
  if (task != NULL)
    xTaskNotifyGive(task);
}

void meshtastic_mqtt_get_snapshot(mt_mqtt_snapshot_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_snapshot;
  out->dropped += s_local_dropped;
  portEXIT_CRITICAL(&s_lock);
}

bool meshtastic_mqtt_is_connected(void) {
  mt_mqtt_snapshot_t snapshot;
  meshtastic_mqtt_get_snapshot(&snapshot);
  return snapshot.is_requested && snapshot.state == MT_MQTT_LINKED;
}

void meshtastic_mqtt_publish(const uint8_t *packet, uint16_t length, const char *channel_name) {
  if (packet == NULL || length == 0 || channel_name == NULL)
    return;
  mt_mqtt_packet_t decoded;
  if (mt_mqtt_parse_packet(packet, length, &decoded) != ESP_OK || !decoded.is_encrypted)
    return;
  mt_mqtt_envelope_t envelope = {.packet = packet, .packet_length = length};
  portENTER_CRITICAL(&s_lock);
  bool can_publish = s_snapshot.is_requested && s_snapshot.state == MT_MQTT_LINKED &&
                     strcmp(channel_name, s_config.channel) == 0 &&
                     decoded.channel == s_config.channel_hash;
  memcpy(envelope.channel, s_config.channel, sizeof(envelope.channel));
  memcpy(envelope.gateway, s_config.gateway, sizeof(envelope.gateway));
  portEXIT_CRITICAL(&s_lock);
  if (!can_publish)
    return;
  mt_mqtt_queued_t message;
  if (mt_mqtt_encode(&envelope, message.data, sizeof(message.data), &message.length) != ESP_OK)
    return;
  portENTER_CRITICAL(&s_lock);
  bool is_duplicate = false;
  for (uint32_t index = 0; index < MQTT_SEEN_SLOTS; ++index)
    if (decoded.id != 0 && s_uplink_seen[index].from == decoded.from &&
        s_uplink_seen[index].id == decoded.id)
      is_duplicate = true;
  if (is_duplicate) {
    portEXIT_CRITICAL(&s_lock);
    return;
  }
  if (s_snapshot.is_requested && s_count < MQTT_TX_SLOTS) {
    s_uplink_seen[s_uplink_index] = (mt_mqtt_seen_t){.from = decoded.from, .id = decoded.id};
    s_uplink_index = (s_uplink_index + 1) % MQTT_SEEN_SLOTS;
    s_queue[(s_head + s_count) % MQTT_TX_SLOTS] = message;
    ++s_count;
  } else {
    ++s_local_dropped;
  }
  TaskHandle_t task = s_task;
  portEXIT_CRITICAL(&s_lock);
  if (task != NULL)
    xTaskNotifyGive(task);
}

void meshtastic_mqtt_publish_radio(const uint8_t *packet, uint16_t length) {
  if (packet == NULL || length <= MT_MQTT_RADIO_HEADER_SIZE ||
      (packet[MQTT_RADIO_FLAGS_OFFSET] & MQTT_RADIO_VIA_MQTT) != 0)
    return;
  char channel[MT_MQTT_NAME_MAX];
  portENTER_CRITICAL(&s_lock);
  bool can_publish = s_snapshot.is_requested && s_snapshot.state == MT_MQTT_LINKED &&
                     packet[MQTT_RADIO_CHANNEL_OFFSET] == s_config.channel_hash;
  memcpy(channel, s_config.channel, sizeof(channel));
  portEXIT_CRITICAL(&s_lock);
  if (!can_publish)
    return;
  uint8_t encoded[SPI_MQTT_PACKET_MAX];
  uint16_t encoded_length = 0;
  if (mt_mqtt_encode_radio(packet, length, encoded, &encoded_length) == ESP_OK)
    meshtastic_mqtt_publish(encoded, encoded_length, channel);
}

static void worker(void *unused) {
  (void)unused;
  bool has_client = false;
  bool has_attempted_client = false;
  uint32_t sequence = 0;
  mt_mqtt_config_t config = {0};
  for (;;) {
    portENTER_CRITICAL(&s_lock);
    bool is_requested = s_snapshot.is_requested;
    if (is_requested && !has_client)
      config = s_config;
    portEXIT_CRITICAL(&s_lock);
    if (!is_requested) {
      esp_err_t error = has_attempted_client ? command(SPI_ID_MQTT_STOP, NULL, 0, NULL) : ESP_OK;
      if (error == ESP_OK) {
        has_client = false;
        has_attempted_client = false;
        portENTER_CRITICAL(&s_lock);
        if (!s_snapshot.is_requested) {
          s_snapshot.state = MT_MQTT_STOPPED;
          s_snapshot.error = ESP_OK;
          s_count = 0;
          memset(s_config.password, 0, sizeof(s_config.password));
        }
        portEXIT_CRITICAL(&s_lock);
        memset(config.password, 0, sizeof(config.password));
      } else {
        set_error(error);
      }
      ulTaskNotifyTake(pdTRUE, has_attempted_client ? pdMS_TO_TICKS(MQTT_POLL_MS) : portMAX_DELAY);
      continue;
    }
    if (!has_client) {
      has_attempted_client = true;
      esp_err_t error = connect_client(&config);
      if (error != ESP_OK) {
        set_error(error);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_POLL_MS));
        continue;
      }
      has_client = true;
      memset(s_seen, 0, sizeof(s_seen));
      s_seen_index = 0;
      portENTER_CRITICAL(&s_lock);
      memset(s_uplink_seen, 0, sizeof(s_uplink_seen));
      s_uplink_index = 0;
      portEXIT_CRITICAL(&s_lock);
    }
    mt_mqtt_queued_t response;
    esp_err_t error = command(SPI_ID_MQTT_STATUS, NULL, 0, &response);
    if (error != ESP_OK || response.length != SPI_MQTT_STATUS_SIZE) {
      set_error(error != ESP_OK ? error : ESP_ERR_INVALID_SIZE);
    } else if ((response.data[0] & SPI_MQTT_FLAG_RUNNING) == 0) {
      has_client = false;
      set_error(ESP_ERR_INVALID_STATE);
    } else {
      bool is_linked = (response.data[0] & (SPI_MQTT_FLAG_CONNECTED | SPI_MQTT_FLAG_SUBSCRIBED)) ==
                       (SPI_MQTT_FLAG_CONNECTED | SPI_MQTT_FLAG_SUBSCRIBED);
      portENTER_CRITICAL(&s_lock);
      if (s_snapshot.is_requested) {
        bool has_error = (response.data[0] & SPI_MQTT_FLAG_ERROR) != 0;
        s_snapshot.state = has_error   ? MT_MQTT_ERROR
                           : is_linked ? MT_MQTT_LINKED
                                       : MT_MQTT_CONNECTING;
        s_snapshot.error = has_error ? ESP_FAIL : ESP_OK;
      }
      s_snapshot.received = read_u32(response.data + MQTT_STATUS_RECEIVED_OFFSET);
      s_snapshot.published = read_u32(response.data + MQTT_STATUS_PUBLISHED_OFFSET);
      s_snapshot.dropped = read_u32(response.data + MQTT_STATUS_DROPPED_OFFSET);
      bool has_message = is_linked && s_count != 0 && s_snapshot.is_requested;
      if (has_message) {
        response = s_queue[s_head];
        s_head = (s_head + 1) % MQTT_TX_SLOTS;
        --s_count;
      }
      portEXIT_CRITICAL(&s_lock);
      if (has_message) {
        if (++sequence == 0)
          ++sequence;
        error = publish_packet(&response, sequence);
        if (error != ESP_OK)
          set_error(error);
      }
      if (is_linked) {
        error = receive_packet(&response);
        if (error == ESP_OK && response.length != 0)
          accept_message(&config, &response);
        else if (error != ESP_OK)
          set_error(error);
      }
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_POLL_MS));
  }
}

static esp_err_t
command(uint16_t id, const uint8_t *payload, uint8_t length, mt_mqtt_queued_t *out) {
  spi_header_t header;
  uint8_t response[SPI_MAX_PAYLOAD];
  esp_err_t error = spi_bridge_send_command(
      id, payload, length, &header, response, sizeof(response), MQTT_SPI_TIMEOUT_MS);
  if (out != NULL)
    out->length = 0;
  if (error != ESP_OK)
    return error;
  if (header.length == 0)
    return ESP_ERR_INVALID_SIZE;
  if (response[0] != SPI_STATUS_OK)
    return response[0] == SPI_STATUS_BUSY ? ESP_ERR_INVALID_STATE : ESP_FAIL;
  if (out != NULL) {
    out->length = header.length - SPI_RESP_STATUS_SIZE;
    memcpy(out->data, response + SPI_RESP_STATUS_SIZE, out->length);
  }
  return ESP_OK;
}

static esp_err_t set_field(uint8_t field, const char *value) {
  uint8_t payload[SPI_MQTT_URI_MAX];
  size_t length = strlen(value);
  if (length >= sizeof(payload))
    return ESP_ERR_INVALID_SIZE;
  payload[0] = field;
  memcpy(payload + 1, value, length);
  return command(SPI_ID_MQTT_CONFIG, payload, length + 1, NULL);
}

static esp_err_t connect_client(const mt_mqtt_config_t *config) {
  esp_err_t error = command(SPI_ID_MQTT_STOP, NULL, 0, NULL);
  if (error != ESP_OK)
    return error;
  char topic[SPI_MQTT_TOPIC_MAX];
  int size = snprintf(topic, sizeof(topic), "%s#", config->topic);
  if (size < 0 || (size_t)size >= sizeof(topic))
    return ESP_ERR_INVALID_SIZE;
  error = set_field(SPI_MQTT_FIELD_URI, config->uri);
  if (error == ESP_OK)
    error = set_field(SPI_MQTT_FIELD_USERNAME, config->username);
  if (error == ESP_OK)
    error = set_field(SPI_MQTT_FIELD_PASSWORD, config->password);
  if (error == ESP_OK)
    error = set_field(SPI_MQTT_FIELD_SUBSCRIBE, topic);
  size = snprintf(topic, sizeof(topic), "%s%s", config->topic, config->gateway);
  if (size < 0 || (size_t)size >= sizeof(topic))
    return ESP_ERR_INVALID_SIZE;
  if (error == ESP_OK)
    error = set_field(SPI_MQTT_FIELD_PUBLISH, topic);
  if (error == ESP_OK)
    error = command(SPI_ID_MQTT_START, NULL, 0, NULL);
  return error;
}

static esp_err_t publish_packet(const mt_mqtt_queued_t *packet, uint32_t sequence) {
  uint16_t offset = 0;
  while (offset < packet->length) {
    uint8_t payload[SPI_MAX_PAYLOAD];
    uint16_t length = packet->length - offset;
    if (length > SPI_MQTT_CHUNK_MAX)
      length = SPI_MQTT_CHUNK_MAX;
    write_u32(payload, sequence);
    write_u16(payload + MQTT_SEQUENCE_SIZE, offset);
    write_u16(payload + MQTT_SEQUENCE_SIZE + MQTT_HALF_SIZE, packet->length);
    memcpy(payload + SPI_MQTT_CHUNK_HEADER_SIZE, packet->data + offset, length);
    esp_err_t error =
        command(SPI_ID_MQTT_PUBLISH, payload, SPI_MQTT_CHUNK_HEADER_SIZE + length, NULL);
    if (error != ESP_OK)
      return error;
    offset += length;
  }
  return ESP_OK;
}

static esp_err_t receive_packet(mt_mqtt_queued_t *packet) {
  uint32_t sequence = 0;
  uint16_t offset = 0, total = 0;
  packet->length = 0;
  do {
    uint8_t request[MQTT_SEQUENCE_SIZE + MQTT_HALF_SIZE];
    write_u32(request, sequence);
    write_u16(request + MQTT_SEQUENCE_SIZE, offset);
    mt_mqtt_queued_t response;
    esp_err_t error = command(SPI_ID_MQTT_RECEIVE, request, sizeof(request), &response);
    if (error != ESP_OK || response.length == 0)
      return error;
    if (response.length <= SPI_MQTT_CHUNK_HEADER_SIZE)
      return ESP_ERR_INVALID_SIZE;
    uint32_t returned_sequence = read_u32(response.data);
    uint16_t returned_offset = read_u16(response.data + MQTT_SEQUENCE_SIZE);
    uint16_t returned_total = read_u16(response.data + MQTT_SEQUENCE_SIZE + MQTT_HALF_SIZE);
    uint16_t length = response.length - SPI_MQTT_CHUNK_HEADER_SIZE;
    if (returned_sequence == 0 || returned_offset != offset || returned_total == 0 ||
        returned_total > sizeof(packet->data) || offset > returned_total ||
        length > returned_total - offset ||
        (sequence != 0 && (returned_sequence != sequence || returned_total != total)))
      return ESP_ERR_INVALID_SIZE;
    sequence = returned_sequence;
    total = returned_total;
    memcpy(packet->data + offset, response.data + SPI_MQTT_CHUNK_HEADER_SIZE, length);
    offset += length;
  } while (offset < total);
  uint8_t ack[MQTT_SEQUENCE_SIZE];
  write_u32(ack, sequence);
  esp_err_t error = command(SPI_ID_MQTT_ACK, ack, sizeof(ack), NULL);
  if (error == ESP_OK)
    packet->length = total;
  return error;
}

static void accept_message(const mt_mqtt_config_t *config, const mt_mqtt_queued_t *message) {
  mt_mqtt_envelope_t envelope;
  mt_mqtt_packet_t packet;
  if (mt_mqtt_decode(message->data, message->length, &envelope) != ESP_OK ||
      strcmp(envelope.channel, config->channel) != 0 ||
      strcmp(envelope.gateway, config->gateway) == 0 ||
      mt_mqtt_parse_packet(envelope.packet, envelope.packet_length, &packet) != ESP_OK ||
      !packet.is_encrypted || packet.channel != config->channel_hash ||
      packet.from == config->node || (packet.to != UINT32_MAX && packet.to != config->node))
    return;
  for (uint32_t index = 0; index < MQTT_SEEN_SLOTS; ++index)
    if (packet.id != 0 && s_seen[index].from == packet.from && s_seen[index].id == packet.id)
      return;
  uint8_t clear[MT_MQTT_RADIO_MAX];
  uint8_t nonce[MQTT_AES_BLOCK] = {0};
  write_u32(nonce, packet.id);
  write_u32(nonce + MQTT_NONCE_NODE_OFFSET, packet.from);
  firmware_crypto_cipher_t params = {.key = {config->key, sizeof(config->key)},
                                     .iv = {nonce, sizeof(nonce)},
                                     .algorithm = PSA_ALG_CTR,
                                     .is_encrypt = false};
  int result = firmware_crypto_cipher(
      &params, (firmware_crypto_span_t){packet.payload, packet.length}, clear);
  char text[MT_MQTT_TEXT_MAX];
  if (result != 0 || mt_mqtt_parse_text(clear, packet.length, text) != ESP_OK)
    return;
  char who[MT_MQTT_NAME_MAX];
  snprintf(who, sizeof(who), "mqtt:!%08lx", (unsigned long)packet.from);
  lora_session_on_rx_text(who, text);
  s_seen[s_seen_index] = (mt_mqtt_seen_t){.from = packet.from, .id = packet.id};
  s_seen_index = (s_seen_index + 1) % MQTT_SEEN_SLOTS;
}

static void set_error(esp_err_t error) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot.state = MT_MQTT_ERROR;
  s_snapshot.error = error;
  portEXIT_CRITICAL(&s_lock);
}
static uint32_t read_u32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << MQTT_BYTE_BITS) |
         ((uint32_t)data[2] << MQTT_HALF_BITS) |
         ((uint32_t)data[3] << (MQTT_BYTE_BITS + MQTT_HALF_BITS));
}
static uint16_t read_u16(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << MQTT_BYTE_BITS);
}
static void write_u32(uint8_t *out, uint32_t value) {
  for (uint32_t index = 0; index < MQTT_SEQUENCE_SIZE; ++index)
    out[index] = (uint8_t)(value >> (MQTT_BYTE_BITS * index));
}
static void write_u16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> MQTT_BYTE_BITS);
}

// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mqtt_bridge.h"

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"

#include "sys_prio.h"

#define MQTT_RX_SLOTS           4
#define MQTT_NETWORK_TIMEOUT_MS 10000
#define MQTT_RECONNECT_MS       15000
#define MQTT_KEEPALIVE_S        60
#define MQTT_TASK_STACK         6144
#define MQTT_IO_BUFFER_SIZE     512
#define MQTT_POLL_REQUEST_SIZE  6
#define MQTT_SEQUENCE_SIZE      4
#define MQTT_BYTE_BITS          8
#define MQTT_HALF_BITS          16
#define MQTT_DEFAULT_SCHEME     "mqtts://"
#define MQTT_PLAIN_SCHEME       "mqtt://"

typedef struct {
  uint32_t sequence;
  uint16_t length;
  uint8_t data[SPI_MQTT_PACKET_MAX];
} mqtt_bridge_packet_t;

static const char *TAG = "MQTT_BRIDGE";
static esp_mqtt_client_handle_t s_client;
static char s_uri[SPI_MQTT_URI_MAX];
static char s_username[SPI_MQTT_CREDENTIAL_MAX];
static char s_password[SPI_MQTT_CREDENTIAL_MAX];
static char s_subscribe[SPI_MQTT_TOPIC_MAX];
static char s_publish[SPI_MQTT_TOPIC_MAX];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_flags;
static int s_subscription_id;
static mqtt_bridge_packet_t s_rx[MQTT_RX_SLOTS];
static uint32_t s_sequence;
static uint32_t s_received, s_published, s_dropped;
static uint8_t s_head, s_count;
static uint8_t s_assembly[SPI_MQTT_PACKET_MAX];
static uint16_t s_assembled, s_expected;
static uint8_t s_tx[SPI_MQTT_PACKET_MAX];
static uint32_t s_tx_sequence, s_last_publish_sequence;
static uint16_t s_tx_length, s_tx_received, s_last_publish_length;

static uint32_t read_u32(const uint8_t *data);
static uint16_t read_u16(const uint8_t *data);
static void write_u32(uint8_t *out, uint32_t value);
static void write_u16(uint8_t *out, uint16_t value);
static void on_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data);
static spi_status_t configure(const mqtt_bridge_request_t *request);
static spi_status_t start(void);
static spi_status_t stop(void);
static spi_status_t publish(const mqtt_bridge_request_t *request);
static spi_status_t receive(const mqtt_bridge_request_t *request);

spi_status_t mqtt_bridge_execute(const mqtt_bridge_request_t *request) {
  if (request == NULL || request->out == NULL || request->out_length == NULL ||
      (request->length != 0 && request->payload == NULL))
    return SPI_STATUS_INVALID_ARG;
  *request->out_length = 0;
  switch (request->command) {
    case SPI_ID_MQTT_CONFIG:
      return configure(request);
    case SPI_ID_MQTT_PUBLISH:
      return publish(request);
    case SPI_ID_MQTT_RECEIVE:
      return receive(request);
    case SPI_ID_MQTT_ACK: {
      if (request->length != MQTT_SEQUENCE_SIZE)
        return SPI_STATUS_INVALID_ARG;
      portENTER_CRITICAL(&s_lock);
      bool is_current = s_count != 0 && s_rx[s_head].sequence == read_u32(request->payload);
      if (is_current) {
        s_head = (s_head + 1) % MQTT_RX_SLOTS;
        --s_count;
      }
      portEXIT_CRITICAL(&s_lock);
      return is_current ? SPI_STATUS_OK : SPI_STATUS_INVALID_ARG;
    }
    case SPI_ID_MQTT_START:
      return request->length == 0 ? start() : SPI_STATUS_INVALID_ARG;
    case SPI_ID_MQTT_STOP:
      return request->length == 0 ? stop() : SPI_STATUS_INVALID_ARG;
    case SPI_ID_MQTT_STATUS:
      if (request->length != 0)
        return SPI_STATUS_INVALID_ARG;
      portENTER_CRITICAL(&s_lock);
      request->out[0] = s_flags;
      write_u32(request->out + 1, s_received);
      write_u32(request->out + 1 + MQTT_SEQUENCE_SIZE, s_published);
      write_u32(request->out + 1 + MQTT_SEQUENCE_SIZE * 2, s_dropped);
      portEXIT_CRITICAL(&s_lock);
      *request->out_length = SPI_MQTT_STATUS_SIZE;
      return SPI_STATUS_OK;
    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}

static spi_status_t configure(const mqtt_bridge_request_t *request) {
  if (s_client != NULL)
    return SPI_STATUS_BUSY;
  if (request->length == 0)
    return SPI_STATUS_INVALID_ARG;
  char *field = NULL;
  size_t capacity = 0;
  switch (request->payload[0]) {
    case SPI_MQTT_FIELD_URI:
      field = s_uri;
      capacity = sizeof(s_uri);
      break;
    case SPI_MQTT_FIELD_USERNAME:
      field = s_username;
      capacity = sizeof(s_username);
      break;
    case SPI_MQTT_FIELD_PASSWORD:
      field = s_password;
      capacity = sizeof(s_password);
      break;
    case SPI_MQTT_FIELD_SUBSCRIBE:
      field = s_subscribe;
      capacity = sizeof(s_subscribe);
      break;
    case SPI_MQTT_FIELD_PUBLISH:
      field = s_publish;
      capacity = sizeof(s_publish);
      break;
    default:
      return SPI_STATUS_INVALID_ARG;
  }
  size_t length = request->length - 1;
  if (length >= capacity || memchr(request->payload + 1, '\0', length) != NULL)
    return SPI_STATUS_INVALID_ARG;
  if ((field == s_subscribe || field == s_publish) && length == 0)
    return SPI_STATUS_INVALID_ARG;
  if (field == s_publish && (memchr(request->payload + 1, '#', length) != NULL ||
                             memchr(request->payload + 1, '+', length) != NULL))
    return SPI_STATUS_INVALID_ARG;
  memcpy(field, request->payload + 1, length);
  field[length] = '\0';
  return SPI_STATUS_OK;
}

static spi_status_t start(void) {
  if (s_client != NULL)
    return SPI_STATUS_BUSY;
  bool is_tls = strncmp(s_uri, MQTT_DEFAULT_SCHEME, sizeof(MQTT_DEFAULT_SCHEME) - 1) == 0;
  bool is_plain = strncmp(s_uri, MQTT_PLAIN_SCHEME, sizeof(MQTT_PLAIN_SCHEME) - 1) == 0;
  if ((!is_tls && !is_plain) || strchr(s_uri, '@') != NULL || s_subscribe[0] == '\0' ||
      s_publish[0] == '\0')
    return SPI_STATUS_INVALID_ARG;
  esp_mqtt_client_config_t config = {
      .broker.address.uri = s_uri,
      .broker.verification.crt_bundle_attach = is_tls ? esp_crt_bundle_attach : NULL,
      .credentials.username = s_username[0] != '\0' ? s_username : NULL,
      .credentials.authentication.password = s_password[0] != '\0' ? s_password : NULL,
      .network.timeout_ms = MQTT_NETWORK_TIMEOUT_MS,
      .network.reconnect_timeout_ms = MQTT_RECONNECT_MS,
      .session.keepalive = MQTT_KEEPALIVE_S,
      .task.priority = SYS_PRIO_SERVICE_LO,
      .task.stack_size = MQTT_TASK_STACK,
      .buffer.size = MQTT_IO_BUFFER_SIZE,
  };
  s_client = esp_mqtt_client_init(&config);
  if (s_client == NULL) {
    ESP_LOGE(TAG, "Client allocation failed");
    return SPI_STATUS_ERROR;
  }
  portENTER_CRITICAL(&s_lock);
  s_flags = SPI_MQTT_FLAG_RUNNING;
  s_count = s_head = 0;
  s_assembled = s_expected = 0;
  s_subscription_id = -1;
  portEXIT_CRITICAL(&s_lock);
  esp_err_t error = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_event, NULL);
  if (error == ESP_OK)
    error = esp_mqtt_client_start(s_client);
  if (error != ESP_OK) {
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    portENTER_CRITICAL(&s_lock);
    s_flags = 0;
    portEXIT_CRITICAL(&s_lock);
    return SPI_STATUS_ERROR;
  }
  return SPI_STATUS_OK;
}

static spi_status_t stop(void) {
  if (s_client != NULL) {
    if (esp_mqtt_client_stop(s_client) != ESP_OK || esp_mqtt_client_destroy(s_client) != ESP_OK)
      return SPI_STATUS_ERROR;
    s_client = NULL;
  }
  portENTER_CRITICAL(&s_lock);
  s_flags = 0;
  s_head = s_count = 0;
  s_assembled = s_expected = 0;
  portEXIT_CRITICAL(&s_lock);
  s_tx_received = s_tx_length = s_last_publish_length = 0;
  s_tx_sequence = s_last_publish_sequence = 0;
  memset(s_password, 0, sizeof(s_password));
  return SPI_STATUS_OK;
}

static spi_status_t publish(const mqtt_bridge_request_t *request) {
  if (request->length <= SPI_MQTT_CHUNK_HEADER_SIZE)
    return SPI_STATUS_INVALID_ARG;
  uint32_t sequence = read_u32(request->payload);
  uint16_t offset = read_u16(request->payload + MQTT_SEQUENCE_SIZE);
  uint16_t total = read_u16(request->payload + MQTT_SEQUENCE_SIZE + sizeof(uint16_t));
  uint16_t length = request->length - SPI_MQTT_CHUNK_HEADER_SIZE;
  if (sequence == 0 || total == 0 || total > sizeof(s_tx) || offset > total ||
      length > total - offset)
    return SPI_STATUS_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  bool can_publish = (s_flags & SPI_MQTT_FLAG_CONNECTED) != 0;
  portEXIT_CRITICAL(&s_lock);
  if (s_client == NULL || !can_publish)
    return SPI_STATUS_BUSY;
  if (sequence == s_last_publish_sequence)
    return total == s_last_publish_length ? SPI_STATUS_OK : SPI_STATUS_INVALID_ARG;
  if (offset == 0 && sequence != s_tx_sequence) {
    s_tx_sequence = sequence;
    s_tx_length = total;
    s_tx_received = 0;
  }
  if (sequence != s_tx_sequence || total != s_tx_length || offset > s_tx_received)
    return SPI_STATUS_INVALID_ARG;
  if (offset < s_tx_received)
    return length <= s_tx_received - offset &&
                   memcmp(s_tx + offset, request->payload + SPI_MQTT_CHUNK_HEADER_SIZE, length) == 0
               ? SPI_STATUS_OK
               : SPI_STATUS_INVALID_ARG;
  memcpy(s_tx + offset, request->payload + SPI_MQTT_CHUNK_HEADER_SIZE, length);
  s_tx_received += length;
  if (s_tx_received != total)
    return SPI_STATUS_OK;
  if (esp_mqtt_client_publish(s_client, s_publish, (const char *)s_tx, total, 0, 0) < 0) {
    s_tx_received = 0;
    return SPI_STATUS_ERROR;
  }
  s_last_publish_sequence = sequence;
  s_last_publish_length = total;
  portENTER_CRITICAL(&s_lock);
  ++s_published;
  portEXIT_CRITICAL(&s_lock);
  return SPI_STATUS_OK;
}

static spi_status_t receive(const mqtt_bridge_request_t *request) {
  if (request->length != MQTT_POLL_REQUEST_SIZE)
    return SPI_STATUS_INVALID_ARG;
  uint32_t sequence = read_u32(request->payload);
  uint16_t offset = read_u16(request->payload + MQTT_SEQUENCE_SIZE);
  portENTER_CRITICAL(&s_lock);
  if (s_count == 0) {
    portEXIT_CRITICAL(&s_lock);
    return SPI_STATUS_OK;
  }
  const mqtt_bridge_packet_t *packet = &s_rx[s_head];
  if ((sequence != packet->sequence && !(sequence == 0 && offset == 0)) ||
      offset >= packet->length) {
    portEXIT_CRITICAL(&s_lock);
    return SPI_STATUS_INVALID_ARG;
  }
  uint16_t length = packet->length - offset;
  if (length > SPI_MQTT_CHUNK_MAX)
    length = SPI_MQTT_CHUNK_MAX;
  write_u32(request->out, packet->sequence);
  write_u16(request->out + MQTT_SEQUENCE_SIZE, offset);
  write_u16(request->out + MQTT_SEQUENCE_SIZE + sizeof(uint16_t), packet->length);
  memcpy(request->out + SPI_MQTT_CHUNK_HEADER_SIZE, packet->data + offset, length);
  portEXIT_CRITICAL(&s_lock);
  *request->out_length = SPI_MQTT_CHUNK_HEADER_SIZE + length;
  return SPI_STATUS_OK;
}

static void on_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
  (void)arg;
  (void)base;
  esp_mqtt_event_handle_t event = event_data;
  if (event_id == MQTT_EVENT_CONNECTED) {
    portENTER_CRITICAL(&s_lock);
    s_flags = SPI_MQTT_FLAG_RUNNING | SPI_MQTT_FLAG_CONNECTED;
    portEXIT_CRITICAL(&s_lock);
    int id = esp_mqtt_client_subscribe(event->client, s_subscribe, 0);
    portENTER_CRITICAL(&s_lock);
    s_subscription_id = id;
    if (id < 0)
      s_flags |= SPI_MQTT_FLAG_ERROR;
    portEXIT_CRITICAL(&s_lock);
  } else if (event_id == MQTT_EVENT_DISCONNECTED || event_id == MQTT_EVENT_ERROR) {
    portENTER_CRITICAL(&s_lock);
    s_flags = (s_flags & SPI_MQTT_FLAG_RUNNING) | SPI_MQTT_FLAG_ERROR;
    s_assembled = s_expected = 0;
    s_subscription_id = -1;
    portEXIT_CRITICAL(&s_lock);
  } else if (event_id == MQTT_EVENT_SUBSCRIBED) {
    portENTER_CRITICAL(&s_lock);
    if (s_subscription_id >= 0 && event->msg_id == s_subscription_id) {
      if (event->data_len == 1 && event->data != NULL && event->data[0] == 0) {
        s_flags = (s_flags & ~SPI_MQTT_FLAG_ERROR) | SPI_MQTT_FLAG_SUBSCRIBED;
      } else {
        s_flags = (s_flags & ~SPI_MQTT_FLAG_SUBSCRIBED) | SPI_MQTT_FLAG_ERROR;
      }
    }
    portEXIT_CRITICAL(&s_lock);
  } else if (event_id == MQTT_EVENT_DATA) {
    portENTER_CRITICAL(&s_lock);
    if (event->current_data_offset == 0) {
      s_assembled = 0;
      s_expected = event->total_data_len > 0 && event->total_data_len <= SPI_MQTT_PACKET_MAX
                       ? event->total_data_len
                       : 0;
    }
    if (s_expected == 0 || event->data == NULL || event->data_len <= 0 ||
        event->total_data_len != s_expected || event->current_data_offset != s_assembled ||
        event->data_len > s_expected - s_assembled) {
      s_expected = s_assembled = 0;
      ++s_dropped;
    } else {
      memcpy(s_assembly + s_assembled, event->data, event->data_len);
      s_assembled += event->data_len;
      if (s_assembled == s_expected) {
        if (s_count < MQTT_RX_SLOTS) {
          uint8_t slot = (s_head + s_count) % MQTT_RX_SLOTS;
          if (++s_sequence == 0)
            ++s_sequence;
          s_rx[slot].sequence = s_sequence;
          s_rx[slot].length = s_expected;
          memcpy(s_rx[slot].data, s_assembly, s_expected);
          ++s_count;
          ++s_received;
        } else {
          ++s_dropped;
        }
        s_expected = s_assembled = 0;
      }
    }
    portEXIT_CRITICAL(&s_lock);
  }
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

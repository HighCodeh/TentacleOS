// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshtastic_mqtt_codec.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#define PB_TAG_BITS            3
#define PB_WIRE_MASK           7
#define PB_VALUE_BITS          7
#define PB_CONTINUE            0x80
#define PB_VALUE_MASK          0x7f
#define PB_VARINT_MAX          10
#define PB_LAST_SHIFT          63
#define PB_FIELD_MAX           0x1fffffffU
#define PB_WIRE_VARINT         0
#define PB_WIRE_FIXED64        1
#define PB_WIRE_BYTES          2
#define PB_WIRE_FIXED32        5
#define PB_FIXED64_SIZE        8
#define PB_FIXED32_SIZE        4
#define PB_ENVELOPE_PACKET     1
#define PB_ENVELOPE_CHANNEL    2
#define PB_ENVELOPE_GATEWAY    3
#define PB_PACKET_FROM         1
#define PB_PACKET_TO           2
#define PB_PACKET_CHANNEL      3
#define PB_PACKET_DECODED      4
#define PB_PACKET_ENCRYPTED    5
#define PB_PACKET_ID           6
#define PB_DATA_PORT           1
#define PB_DATA_PAYLOAD        2
#define PB_TEXT_PORT           1
#define RADIO_CHANNEL_OFFSET   13
#define PB_BYTE_BITS           8
#define UTF8_ASCII_MAX         0x7f
#define UTF8_CONTINUATION_MASK 0xc0
#define UTF8_CONTINUATION_TAG  0x80
#define UTF8_VALUE_MASK        0x3f
#define UTF8_VALUE_BITS        6
#define UTF8_TWO_MIN           0xc2
#define UTF8_THREE_MIN         0xe0
#define UTF8_FOUR_MIN          0xf0
#define UTF8_FOUR_MAX          0xf4
#define UTF8_TWO_VALUE_MIN     0x80
#define UTF8_THREE_VALUE_MIN   0x800
#define UTF8_FOUR_VALUE_MIN    0x10000
#define UTF8_SCALAR_MAX        0x10ffff
#define UTF8_SURROGATE_MIN     0xd800
#define UTF8_SURROGATE_MAX     0xdfff

static const char *TAG = "MT_MQTT_CODEC";

typedef struct {
  const uint8_t *data;
  uint16_t length;
  uint16_t offset;
} mt_mqtt_cursor_t;

typedef struct {
  uint32_t number;
  uint8_t wire;
  uint64_t value;
  const uint8_t *bytes;
  uint16_t length;
} mt_mqtt_field_t;

static bool is_utf8(const uint8_t *data, uint16_t length);
static bool read_varint(mt_mqtt_cursor_t *cursor, uint64_t *out);
static bool read_field(mt_mqtt_cursor_t *cursor, mt_mqtt_field_t *out);
static bool copy_name(const mt_mqtt_field_t *field, char *out);
static uint16_t put_varint(uint8_t *out, uint32_t value);
static uint32_t read_fixed32(const uint8_t *data);

esp_err_t mt_mqtt_decode(const uint8_t *data, uint16_t length, mt_mqtt_envelope_t *out) {
  if (data == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  mt_mqtt_cursor_t cursor = {.data = data, .length = length};
  while (cursor.offset < cursor.length) {
    mt_mqtt_field_t field;
    if (!read_field(&cursor, &field))
      return ESP_ERR_INVALID_SIZE;
    if (field.number == PB_ENVELOPE_PACKET) {
      if (field.wire != PB_WIRE_BYTES || out->packet != NULL || field.length == 0)
        return ESP_ERR_INVALID_ARG;
      out->packet = field.bytes;
      out->packet_length = field.length;
    } else if (field.number == PB_ENVELOPE_CHANNEL) {
      if (out->channel[0] != '\0' || !copy_name(&field, out->channel))
        return ESP_ERR_INVALID_ARG;
    } else if (field.number == PB_ENVELOPE_GATEWAY) {
      if (out->gateway[0] != '\0' || !copy_name(&field, out->gateway))
        return ESP_ERR_INVALID_ARG;
    }
  }
  return out->packet != NULL && out->channel[0] != '\0' && out->gateway[0] != '\0'
             ? ESP_OK
             : ESP_ERR_INVALID_ARG;
}

esp_err_t mt_mqtt_parse_packet(const uint8_t *data, uint16_t length, mt_mqtt_packet_t *out) {
  if (data == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  uint32_t seen = 0;
  mt_mqtt_cursor_t cursor = {.data = data, .length = length};
  while (cursor.offset < cursor.length) {
    mt_mqtt_field_t field;
    if (!read_field(&cursor, &field))
      return ESP_ERR_INVALID_SIZE;
    if (field.number > PB_PACKET_ID)
      continue;
    uint32_t bit = 1U << field.number;
    if ((seen & bit) != 0)
      return ESP_ERR_INVALID_ARG;
    seen |= bit;
    switch (field.number) {
      case PB_PACKET_FROM:
      case PB_PACKET_TO:
      case PB_PACKET_ID:
        if (field.wire != PB_WIRE_FIXED32)
          return ESP_ERR_INVALID_ARG;
        if (field.number == PB_PACKET_FROM)
          out->from = (uint32_t)field.value;
        if (field.number == PB_PACKET_TO)
          out->to = (uint32_t)field.value;
        if (field.number == PB_PACKET_ID)
          out->id = (uint32_t)field.value;
        break;
      case PB_PACKET_CHANNEL:
        if (field.wire != PB_WIRE_VARINT || field.value > UINT8_MAX)
          return ESP_ERR_INVALID_ARG;
        out->channel = (uint8_t)field.value;
        break;
      case PB_PACKET_DECODED:
      case PB_PACKET_ENCRYPTED:
        if (field.wire != PB_WIRE_BYTES || out->payload != NULL || field.length == 0 ||
            field.length > MT_MQTT_RADIO_MAX - MT_MQTT_RADIO_HEADER_SIZE)
          return ESP_ERR_INVALID_ARG;
        out->payload = field.bytes;
        out->length = field.length;
        out->is_encrypted = field.number == PB_PACKET_ENCRYPTED;
        break;
      default:
        break;
    }
  }
  uint32_t required = (1U << PB_PACKET_FROM) | (1U << PB_PACKET_TO) | (1U << PB_PACKET_ID);
  return (seen & required) == required && out->payload != NULL ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t mt_mqtt_parse_text(const uint8_t *data, uint16_t length, char out[MT_MQTT_TEXT_MAX]) {
  if (data == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  out[0] = '\0';
  bool has_port = false, has_text = false;
  mt_mqtt_cursor_t cursor = {.data = data, .length = length};
  while (cursor.offset < cursor.length) {
    mt_mqtt_field_t field;
    if (!read_field(&cursor, &field))
      return ESP_ERR_INVALID_SIZE;
    if (field.number == PB_DATA_PORT) {
      if (has_port || field.wire != PB_WIRE_VARINT || field.value != PB_TEXT_PORT)
        return ESP_ERR_NOT_SUPPORTED;
      has_port = true;
    } else if (field.number == PB_DATA_PAYLOAD) {
      if (has_text || field.wire != PB_WIRE_BYTES || field.length == 0 ||
          field.length >= MT_MQTT_TEXT_MAX || memchr(field.bytes, '\0', field.length) != NULL ||
          !is_utf8(field.bytes, field.length))
        return ESP_ERR_INVALID_ARG;
      memcpy(out, field.bytes, field.length);
      out[field.length] = '\0';
      has_text = true;
    }
  }
  return has_port && has_text ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t mt_mqtt_encode(const mt_mqtt_envelope_t *envelope,
                         uint8_t *out,
                         uint16_t capacity,
                         uint16_t *out_length) {
  if (envelope == NULL || out == NULL || out_length == NULL || envelope->packet == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_length = 0;
  size_t channel_length = strnlen(envelope->channel, sizeof(envelope->channel));
  size_t gateway_length = strnlen(envelope->gateway, sizeof(envelope->gateway));
  if (envelope->packet_length == 0 || channel_length == 0 || gateway_length == 0 ||
      channel_length == sizeof(envelope->channel) || gateway_length == sizeof(envelope->gateway))
    return ESP_ERR_INVALID_ARG;
  uint32_t needed = envelope->packet_length + channel_length + gateway_length + PB_FIXED64_SIZE;
  if (needed > capacity)
    return ESP_ERR_INVALID_SIZE;
  uint16_t offset = 0;
  out[offset++] = (PB_ENVELOPE_PACKET << PB_TAG_BITS) | PB_WIRE_BYTES;
  offset += put_varint(out + offset, envelope->packet_length);
  memcpy(out + offset, envelope->packet, envelope->packet_length);
  offset += envelope->packet_length;
  out[offset++] = (PB_ENVELOPE_CHANNEL << PB_TAG_BITS) | PB_WIRE_BYTES;
  offset += put_varint(out + offset, channel_length);
  memcpy(out + offset, envelope->channel, channel_length);
  offset += channel_length;
  out[offset++] = (PB_ENVELOPE_GATEWAY << PB_TAG_BITS) | PB_WIRE_BYTES;
  offset += put_varint(out + offset, gateway_length);
  memcpy(out + offset, envelope->gateway, gateway_length);
  offset += gateway_length;
  *out_length = offset;
  return ESP_OK;
}

esp_err_t
mt_mqtt_encode_radio(const uint8_t *data, uint16_t length, uint8_t *out, uint16_t *out_length) {
  if (data == NULL || out == NULL || out_length == NULL || length <= MT_MQTT_RADIO_HEADER_SIZE ||
      length > MT_MQTT_RADIO_MAX)
    return ESP_ERR_INVALID_ARG;
  uint16_t offset = 0;
  const uint8_t fields[] = {PB_PACKET_FROM, PB_PACKET_TO, PB_PACKET_ID};
  const uint8_t offsets[] = {PB_FIXED32_SIZE, 0, PB_FIXED32_SIZE * 2};
  for (uint32_t index = 0; index < sizeof(fields); ++index) {
    out[offset++] = (fields[index] << PB_TAG_BITS) | PB_WIRE_FIXED32;
    memcpy(out + offset, data + offsets[index], PB_FIXED32_SIZE);
    offset += PB_FIXED32_SIZE;
  }
  out[offset++] = (PB_PACKET_CHANNEL << PB_TAG_BITS) | PB_WIRE_VARINT;
  offset += put_varint(out + offset, data[RADIO_CHANNEL_OFFSET]);
  out[offset++] = (PB_PACKET_ENCRYPTED << PB_TAG_BITS) | PB_WIRE_BYTES;
  offset += put_varint(out + offset, length - MT_MQTT_RADIO_HEADER_SIZE);
  memcpy(out + offset, data + MT_MQTT_RADIO_HEADER_SIZE, length - MT_MQTT_RADIO_HEADER_SIZE);
  offset += length - MT_MQTT_RADIO_HEADER_SIZE;
  *out_length = offset;
  return ESP_OK;
}

static bool read_varint(mt_mqtt_cursor_t *cursor, uint64_t *out) {
  *out = 0;
  for (uint32_t index = 0; index < PB_VARINT_MAX && cursor->offset < cursor->length; ++index) {
    uint8_t byte = cursor->data[cursor->offset++];
    uint32_t shift = index * PB_VALUE_BITS;
    if (shift == PB_LAST_SHIFT && byte > 1)
      return false;
    *out |= (uint64_t)(byte & PB_VALUE_MASK) << shift;
    if ((byte & PB_CONTINUE) == 0)
      return true;
  }
  return false;
}

static bool read_field(mt_mqtt_cursor_t *cursor, mt_mqtt_field_t *out) {
  uint64_t tag;
  memset(out, 0, sizeof(*out));
  if (!read_varint(cursor, &tag) || (tag >> PB_TAG_BITS) == 0 ||
      (tag >> PB_TAG_BITS) > PB_FIELD_MAX)
    return false;
  out->number = (uint32_t)(tag >> PB_TAG_BITS);
  out->wire = tag & PB_WIRE_MASK;
  if (out->wire == PB_WIRE_VARINT)
    return read_varint(cursor, &out->value);
  uint64_t length = out->wire == PB_WIRE_FIXED32 ? PB_FIXED32_SIZE : PB_FIXED64_SIZE;
  if (out->wire == PB_WIRE_BYTES) {
    if (!read_varint(cursor, &length))
      return false;
  } else if (out->wire != PB_WIRE_FIXED32 && out->wire != PB_WIRE_FIXED64) {
    ESP_LOGD(TAG, "Unsupported protobuf wire type");
    return false;
  }
  if (length > (uint64_t)(cursor->length - cursor->offset))
    return false;
  out->bytes = cursor->data + cursor->offset;
  out->length = (uint16_t)length;
  if (out->wire == PB_WIRE_FIXED32)
    out->value = read_fixed32(out->bytes);
  cursor->offset += (uint16_t)length;
  return true;
}

static bool copy_name(const mt_mqtt_field_t *field, char *out) {
  if (field->wire != PB_WIRE_BYTES || field->length == 0 || field->length >= MT_MQTT_NAME_MAX ||
      memchr(field->bytes, '\0', field->length) != NULL)
    return false;
  memcpy(out, field->bytes, field->length);
  out[field->length] = '\0';
  return true;
}

static uint16_t put_varint(uint8_t *out, uint32_t value) {
  uint16_t count = 0;
  do {
    uint8_t byte = value & PB_VALUE_MASK;
    value >>= PB_VALUE_BITS;
    out[count++] = byte | (value != 0 ? PB_CONTINUE : 0);
  } while (value != 0);
  return count;
}

static uint32_t read_fixed32(const uint8_t *data) {
  uint32_t value = 0;
  for (uint32_t index = 0; index < PB_FIXED32_SIZE; ++index)
    value |= (uint32_t)data[index] << (PB_BYTE_BITS * index);
  return value;
}

static bool is_utf8(const uint8_t *data, uint16_t length) {
  uint16_t offset = 0;
  while (offset < length) {
    uint8_t first = data[offset++];
    if (first <= UTF8_ASCII_MAX)
      continue;
    uint8_t remaining;
    uint32_t value, minimum;
    if (first >= UTF8_TWO_MIN && first < UTF8_THREE_MIN) {
      remaining = 1;
      minimum = UTF8_TWO_VALUE_MIN;
      value = first & (UTF8_VALUE_MASK >> 1);
    } else if (first >= UTF8_THREE_MIN && first < UTF8_FOUR_MIN) {
      remaining = 2;
      minimum = UTF8_THREE_VALUE_MIN;
      value = first & (UTF8_VALUE_MASK >> 2);
    } else if (first >= UTF8_FOUR_MIN && first <= UTF8_FOUR_MAX) {
      remaining = 3;
      minimum = UTF8_FOUR_VALUE_MIN;
      value = first & (UTF8_VALUE_MASK >> 3);
    } else {
      return false;
    }
    if (remaining > length - offset)
      return false;
    for (uint8_t index = 0; index < remaining; ++index) {
      uint8_t next = data[offset++];
      if ((next & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_TAG)
        return false;
      value = (value << UTF8_VALUE_BITS) | (next & UTF8_VALUE_MASK);
    }
    if (value < minimum || value > UTF8_SCALAR_MAX ||
        (value >= UTF8_SURROGATE_MIN && value <= UTF8_SURROGATE_MAX))
      return false;
  }
  return true;
}

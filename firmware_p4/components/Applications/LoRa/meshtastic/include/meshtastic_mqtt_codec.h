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

#ifndef MESHTASTIC_MQTT_CODEC_H
#define MESHTASTIC_MQTT_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MT_MQTT_NAME_MAX          32
#define MT_MQTT_RADIO_HEADER_SIZE 16
#define MT_MQTT_RADIO_MAX         255
#define MT_MQTT_TEXT_MAX          160

typedef struct {
  const uint8_t *packet;
  uint16_t packet_length;
  char channel[MT_MQTT_NAME_MAX];
  char gateway[MT_MQTT_NAME_MAX];
} mt_mqtt_envelope_t;

typedef struct {
  uint32_t from;
  uint32_t to;
  uint32_t id;
  uint8_t channel;
  bool is_encrypted;
  const uint8_t *payload;
  uint16_t length;
} mt_mqtt_packet_t;

/** Output views borrow the input buffer for the duration of the caller's operation. */
esp_err_t mt_mqtt_decode(const uint8_t *data, uint16_t length, mt_mqtt_envelope_t *out);
esp_err_t mt_mqtt_parse_packet(const uint8_t *data, uint16_t length, mt_mqtt_packet_t *out);
esp_err_t mt_mqtt_parse_text(const uint8_t *data, uint16_t length, char out[MT_MQTT_TEXT_MAX]);
esp_err_t mt_mqtt_encode(const mt_mqtt_envelope_t *envelope,
                         uint8_t *out,
                         uint16_t capacity,
                         uint16_t *out_length);
/** out must provide MT_MQTT_RADIO_MAX + 32 bytes for protobuf framing. */
esp_err_t
mt_mqtt_encode_radio(const uint8_t *data, uint16_t length, uint8_t *out, uint16_t *out_length);

#ifdef __cplusplus
}
#endif
#endif // MESHTASTIC_MQTT_CODEC_H

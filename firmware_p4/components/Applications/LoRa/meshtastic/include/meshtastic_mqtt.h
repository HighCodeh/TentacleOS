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

#ifndef MESHTASTIC_MQTT_H
#define MESHTASTIC_MQTT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  MT_MQTT_STOPPED = 0,
  MT_MQTT_CONNECTING,
  MT_MQTT_LINKED,
  MT_MQTT_STOPPING,
  MT_MQTT_ERROR,
  MT_MQTT_STATE_COUNT
} mt_mqtt_state_t;

typedef struct {
  mt_mqtt_state_t state;
  bool is_requested;
  esp_err_t error;
  uint32_t received;
  uint32_t published;
  uint32_t dropped;
} mt_mqtt_snapshot_t;

/** Queue an on-demand C5 MQTT connection using the current primary Meshtastic channel.
 *  ESP_OK means queued; LINKED requires actual broker connection and SUBACK. */
esp_err_t meshtastic_mqtt_init(const char *broker_uri, const char *username, const char *password);
void meshtastic_mqtt_stop(void);
void meshtastic_mqtt_get_snapshot(mt_mqtt_snapshot_t *out);
bool meshtastic_mqtt_is_connected(void);
/** Queue an encrypted MeshPacket in a ServiceEnvelope; never blocks on the network. */
void meshtastic_mqtt_publish(const uint8_t *packet, uint16_t length, const char *channel_name);
/** Queue an encrypted on-air packet after native radio TX acceptance. */
void meshtastic_mqtt_publish_radio(const uint8_t *packet, uint16_t length);

#ifdef __cplusplus
}
#endif
#endif // MESHTASTIC_MQTT_H

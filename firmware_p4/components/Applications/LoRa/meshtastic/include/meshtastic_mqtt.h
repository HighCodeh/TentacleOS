// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef MESHTASTIC_MQTT_H
#define MESHTASTIC_MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Initialize MQTT uplink client. Connects when WiFi STA link is
 *        up. Publishes MeshPackets received from LoRa to
 *        `msh/{region}/2/e/{channel_name}/{node_id}` topic.
 *
 * @param broker_uri  e.g. "mqtt://mqtt.meshtastic.org:1883"
 * @param username    NULL if anonymous
 * @param password    NULL if anonymous
 */
esp_err_t meshtastic_mqtt_init(const char *broker_uri,
                                const char *username,
                                const char *password);

/**
 * @brief Publish an already-encoded MeshPacket proto to the uplink.
 *        Called from the mesh RX path for packets we want to uplink.
 */
void meshtastic_mqtt_publish(const uint8_t *meshpacket_pb, uint16_t len,
                              const char *channel_name);

bool meshtastic_mqtt_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif

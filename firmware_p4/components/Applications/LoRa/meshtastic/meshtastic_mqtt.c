// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#include "meshtastic_mqtt.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MT_MQTT";

#define MT_MQTT_REGION_DEFAULT "US"

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static char s_region[8] = MT_MQTT_REGION_DEFAULT;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)handler_args; (void)base; (void)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

esp_err_t meshtastic_mqtt_init(const char *broker_uri,
                                const char *username,
                                const char *password)
{
    if (s_client != NULL) return ESP_OK;

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = broker_uri,
        .credentials.username = username,
        .credentials.authentication.password = password,
        .network.timeout_ms = 10000,
        .network.reconnect_timeout_ms = 15000,
        .session.keepalive = 60,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) return ESP_FAIL;

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY,
                                     mqtt_event_handler, NULL);
    esp_err_t rc = esp_mqtt_client_start(s_client);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(rc));
        return rc;
    }
    ESP_LOGI(TAG, "MQTT client started (broker=%s)", broker_uri);
    return ESP_OK;
}

void meshtastic_mqtt_publish(const uint8_t *meshpacket_pb, uint16_t len,
                              const char *channel_name)
{
    if (!s_connected || s_client == NULL) return;

    char topic[80];
    snprintf(topic, sizeof(topic), "msh/%s/2/e/%s/broadcast",
             s_region, channel_name ? channel_name : "LongFast");

    int msg_id = esp_mqtt_client_publish(s_client, topic,
                                           (const char *)meshpacket_pb, len,
                                           0, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish failed");
    } else {
        ESP_LOGD(TAG, "publish %s (%u bytes)", topic, len);
    }
}

bool meshtastic_mqtt_is_connected(void)
{
    return s_connected;
}

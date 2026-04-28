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

#include "meshtastic_nvs.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "MESHTASTIC_NVS";

#define MT_NVS_NAMESPACE "meshtastic"
#define MT_NVS_VER_KEY   "_ver"

static nvs_handle_t s_handle = 0;
static bool s_is_inited = false;

esp_err_t mt_nvs_init(void)
{
    if (s_is_inited) return ESP_OK;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupt, erasing partition");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(MT_NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint32_t stored_ver = 0;
    nvs_get_u32(s_handle, MT_NVS_VER_KEY, &stored_ver);
    if (stored_ver != MT_NVS_VERSION) {
        ESP_LOGW(TAG, "Schema version %lu -> %d, wiping namespace",
                 (unsigned long)stored_ver, MT_NVS_VERSION);
        nvs_erase_all(s_handle);
        nvs_set_u32(s_handle, MT_NVS_VER_KEY, MT_NVS_VERSION);
        nvs_commit(s_handle);
    }

    s_is_inited = true;
    ESP_LOGI(TAG, "Initialized - namespace='%s', version=%d",
             MT_NVS_NAMESPACE, MT_NVS_VERSION);
    return ESP_OK;
}

esp_err_t mt_nvs_set_blob(const char *key, const void *data, size_t len)
{
    if (!s_is_inited) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_blob(s_handle, key, data, len);
    if (ret == ESP_OK) nvs_commit(s_handle);
    return ret;
}

int mt_nvs_get_blob(const char *key, void *out_data, size_t max_len)
{
    if (!s_is_inited) return -1;
    size_t len = max_len;
    esp_err_t ret = nvs_get_blob(s_handle, key, out_data, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (ret != ESP_OK) return -1;
    return (int)len;
}

esp_err_t mt_nvs_set_u32(const char *key, uint32_t value)
{
    if (!s_is_inited) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_u32(s_handle, key, value);
    if (ret == ESP_OK) nvs_commit(s_handle);
    return ret;
}

uint32_t mt_nvs_get_u32(const char *key, uint32_t default_value)
{
    if (!s_is_inited) return default_value;
    uint32_t value = default_value;
    nvs_get_u32(s_handle, key, &value);
    return value;
}

esp_err_t mt_nvs_erase(const char *key)
{
    if (!s_is_inited) return ESP_ERR_INVALID_STATE;
    return nvs_erase_key(s_handle, key);
}

esp_err_t mt_nvs_factory_reset(void)
{
    if (!s_is_inited) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_erase_all(s_handle);
    if (ret == ESP_OK) {
        nvs_set_u32(s_handle, MT_NVS_VER_KEY, MT_NVS_VERSION);
        nvs_commit(s_handle);
        ESP_LOGW(TAG, "Factory reset complete");
    }
    return ret;
}

esp_err_t mt_nvs_commit(void)
{
    if (!s_is_inited) return ESP_ERR_INVALID_STATE;
    return nvs_commit(s_handle);
}

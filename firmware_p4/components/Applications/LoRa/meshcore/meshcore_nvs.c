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

#include "meshcore_nvs.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "MC_NVS";

static bool s_is_initialized = false;

esp_err_t mc_nvs_init(void) {
  if (s_is_initialized)
    return ESP_OK;
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition needs erase, recreating");
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
    return err;
  }
  s_is_initialized = true;
  return ESP_OK;
}

esp_err_t mc_nvs_get_blob(const char *key, void *out, size_t *len) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(MC_NVS_NAMESPACE, NVS_READONLY, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_get_blob(h, key, out, len);
  nvs_close(h);
  return err;
}

esp_err_t mc_nvs_set_blob(const char *key, const void *in, size_t len) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(MC_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_set_blob(h, key, in, len);
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  return err;
}

esp_err_t mc_nvs_get_u32(const char *key, uint32_t *out) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(MC_NVS_NAMESPACE, NVS_READONLY, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_get_u32(h, key, out);
  nvs_close(h);
  return err;
}

esp_err_t mc_nvs_set_u32(const char *key, uint32_t val) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(MC_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_set_u32(h, key, val);
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  return err;
}

esp_err_t mc_nvs_get_str(const char *key, char *out, size_t max) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(MC_NVS_NAMESPACE, NVS_READONLY, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_get_str(h, key, out, &max);
  nvs_close(h);
  return err;
}

esp_err_t mc_nvs_set_str(const char *key, const char *val) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(MC_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_set_str(h, key, val);
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  return err;
}

esp_err_t mc_nvs_erase(const char *key) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(MC_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_erase_key(h, key);
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return ESP_OK;
  return err;
}

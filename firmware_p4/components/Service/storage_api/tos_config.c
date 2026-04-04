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

#include "tos_config.h"
#include "tos_storage_paths.h"
#include "esp_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tos_config";

// Global config instances with defaults

tos_config_screen_t g_config_screen = {
  .brightness       = 80,
  .rotation         = 1,
  .theme            = "default",
  .auto_lock_seconds = 300,
  .auto_dim         = true,
};

tos_config_wifi_t g_config_wifi = {
  .ap = {
    .ssid     = "TentacleOS",
    .password  = "12345678",
    .ip_addr   = "192.168.4.1",
    .max_conn  = 4,
    .enabled   = true,
  },
  .client = {
    .enabled  = false,
    .ssid     = "",
    .password  = "",
  },
};

tos_config_ble_t g_config_ble = {
  .name    = "TentacleOS",
  .enabled = true,
};

tos_config_lora_t g_config_lora = {
  .frequency        = 915000000,
  .spreading_factor = 7,
  .bandwidth        = 125000,
  .tx_power         = 14,
  .sync_word        = 0x12,
  .enabled          = false,
};

tos_config_system_t g_config_system = {
  .name            = "TentacleOS",
  .locale          = "pt-BR",
  .timezone_offset = -3,
  .volume          = 50,
  .vibration       = true,
  .log_level       = "info",
  .first_boot_done = false,
};

// Helpers

static char *read_file_to_string(const char *path)
{
  FILE *f = fopen(path, "r");
  if (!f) return NULL;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return NULL;
  }

  char *buf = malloc(size + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  fread(buf, 1, size, f);
  buf[size] = '\0';
  fclose(f);
  return buf;
}

static esp_err_t write_string_to_file(const char *path, const char *data)
{
  FILE *f = fopen(path, "w");
  if (!f) return ESP_FAIL;

  fputs(data, f);
  fclose(f);
  return ESP_OK;
}

static void json_get_str(cJSON *obj, const char *key, char *dst, size_t max)
{
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (cJSON_IsString(item) && item->valuestring) {
    strncpy(dst, item->valuestring, max - 1);
    dst[max - 1] = '\0';
  }
}

static int json_get_int(cJSON *obj, const char *key, int fallback)
{
  cJSON *item = cJSON_GetObjectItem(obj, key);
  return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool json_get_bool(cJSON *obj, const char *key, bool fallback)
{
  cJSON *item = cJSON_GetObjectItem(obj, key);
  return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

// Parse per module 

static void parse_screen(cJSON *root)
{
  g_config_screen.brightness        = json_get_int(root, "brightness", g_config_screen.brightness);
  g_config_screen.rotation          = json_get_int(root, "rotation", g_config_screen.rotation);
  g_config_screen.auto_lock_seconds = json_get_int(root, "auto_lock_seconds", g_config_screen.auto_lock_seconds);
  g_config_screen.auto_dim          = json_get_bool(root, "auto_dim", g_config_screen.auto_dim);
  json_get_str(root, "theme", g_config_screen.theme, sizeof(g_config_screen.theme));
}

static void parse_wifi(cJSON *root)
{
  cJSON *ap = cJSON_GetObjectItem(root, "ap");
  if (ap) {
    json_get_str(ap, "ssid", g_config_wifi.ap.ssid, sizeof(g_config_wifi.ap.ssid));
    json_get_str(ap, "password", g_config_wifi.ap.password, sizeof(g_config_wifi.ap.password));
    json_get_str(ap, "ip_addr", g_config_wifi.ap.ip_addr, sizeof(g_config_wifi.ap.ip_addr));
    g_config_wifi.ap.max_conn = json_get_int(ap, "max_conn", g_config_wifi.ap.max_conn);
    g_config_wifi.ap.enabled  = json_get_bool(ap, "enabled", g_config_wifi.ap.enabled);
  }

  cJSON *client = cJSON_GetObjectItem(root, "client");
  if (client) {
    g_config_wifi.client.enabled = json_get_bool(client, "enabled", g_config_wifi.client.enabled);
    json_get_str(client, "ssid", g_config_wifi.client.ssid, sizeof(g_config_wifi.client.ssid));
    json_get_str(client, "password", g_config_wifi.client.password, sizeof(g_config_wifi.client.password));
  }
}

static void parse_ble(cJSON *root)
{
  json_get_str(root, "name", g_config_ble.name, sizeof(g_config_ble.name));
  g_config_ble.enabled = json_get_bool(root, "enabled", g_config_ble.enabled);
}

static void parse_lora(cJSON *root)
{
  g_config_lora.frequency        = (uint32_t)json_get_int(root, "frequency", (int)g_config_lora.frequency);
  g_config_lora.spreading_factor = json_get_int(root, "spreading_factor", g_config_lora.spreading_factor);
  g_config_lora.bandwidth        = (long)json_get_int(root, "bandwidth", (int)g_config_lora.bandwidth);
  g_config_lora.tx_power         = json_get_int(root, "tx_power", g_config_lora.tx_power);
  g_config_lora.sync_word        = (uint8_t)json_get_int(root, "sync_word", g_config_lora.sync_word);
  g_config_lora.enabled          = json_get_bool(root, "enabled", g_config_lora.enabled);
}

static void parse_system(cJSON *root)
{
  json_get_str(root, "name", g_config_system.name, sizeof(g_config_system.name));
  json_get_str(root, "locale", g_config_system.locale, sizeof(g_config_system.locale));
  json_get_str(root, "log_level", g_config_system.log_level, sizeof(g_config_system.log_level));
  g_config_system.timezone_offset = json_get_int(root, "timezone_offset", g_config_system.timezone_offset);
  g_config_system.volume          = json_get_int(root, "volume", g_config_system.volume);
  g_config_system.vibration       = json_get_bool(root, "vibration", g_config_system.vibration);
  g_config_system.first_boot_done = json_get_bool(root, "first_boot_done", g_config_system.first_boot_done);
}

// Serialize per module 

static cJSON *serialize_screen(void)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "brightness", g_config_screen.brightness);
  cJSON_AddNumberToObject(root, "rotation", g_config_screen.rotation);
  cJSON_AddStringToObject(root, "theme", g_config_screen.theme);
  cJSON_AddNumberToObject(root, "auto_lock_seconds", g_config_screen.auto_lock_seconds);
  cJSON_AddBoolToObject(root, "auto_dim", g_config_screen.auto_dim);
  return root;
}

static cJSON *serialize_wifi(void)
{
  cJSON *root = cJSON_CreateObject();

  cJSON *ap = cJSON_AddObjectToObject(root, "ap");
  cJSON_AddStringToObject(ap, "ssid", g_config_wifi.ap.ssid);
  cJSON_AddStringToObject(ap, "password", g_config_wifi.ap.password);
  cJSON_AddStringToObject(ap, "ip_addr", g_config_wifi.ap.ip_addr);
  cJSON_AddNumberToObject(ap, "max_conn", g_config_wifi.ap.max_conn);
  cJSON_AddBoolToObject(ap, "enabled", g_config_wifi.ap.enabled);

  cJSON *client = cJSON_AddObjectToObject(root, "client");
  cJSON_AddBoolToObject(client, "enabled", g_config_wifi.client.enabled);
  cJSON_AddStringToObject(client, "ssid", g_config_wifi.client.ssid);
  cJSON_AddStringToObject(client, "password", g_config_wifi.client.password);

  return root;
}

static cJSON *serialize_ble(void)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", g_config_ble.name);
  cJSON_AddBoolToObject(root, "enabled", g_config_ble.enabled);
  return root;
}

static cJSON *serialize_lora(void)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "frequency", g_config_lora.frequency);
  cJSON_AddNumberToObject(root, "spreading_factor", g_config_lora.spreading_factor);
  cJSON_AddNumberToObject(root, "bandwidth", g_config_lora.bandwidth);
  cJSON_AddNumberToObject(root, "tx_power", g_config_lora.tx_power);
  cJSON_AddNumberToObject(root, "sync_word", g_config_lora.sync_word);
  cJSON_AddBoolToObject(root, "enabled", g_config_lora.enabled);
  return root;
}

static cJSON *serialize_system(void)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", g_config_system.name);
  cJSON_AddStringToObject(root, "locale", g_config_system.locale);
  cJSON_AddNumberToObject(root, "timezone_offset", g_config_system.timezone_offset);
  cJSON_AddNumberToObject(root, "volume", g_config_system.volume);
  cJSON_AddBoolToObject(root, "vibration", g_config_system.vibration);
  cJSON_AddStringToObject(root, "log_level", g_config_system.log_level);
  cJSON_AddBoolToObject(root, "first_boot_done", g_config_system.first_boot_done);
  return root;
}

// Public API

esp_err_t tos_config_load(const char *sd_path, const char *flash_path, const char *module)
{
  char *json_str = read_file_to_string(sd_path);
  const char *source = "SD";

  if (!json_str && flash_path) {
    json_str = read_file_to_string(flash_path);
    source = "flash";
  }

  if (!json_str) {
    ESP_LOGW(TAG, "[%s] No config found, using defaults", module);
    return ESP_ERR_NOT_FOUND;
  }

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);

  if (!root) {
    ESP_LOGE(TAG, "[%s] JSON parse failed from %s", module, source);
    return ESP_FAIL;
  }

  if (strcmp(module, "screen") == 0)      parse_screen(root);
  else if (strcmp(module, "wifi") == 0)   parse_wifi(root);
  else if (strcmp(module, "ble") == 0)    parse_ble(root);
  else if (strcmp(module, "lora") == 0)   parse_lora(root);
  else if (strcmp(module, "system") == 0) parse_system(root);

  cJSON_Delete(root);
  ESP_LOGI(TAG, "[%s] Loaded from %s", module, source);
  return ESP_OK;
}

void tos_config_load_all(void)
{
  tos_config_load(TOS_PATH_CONFIG_SCREEN, ASSETS_CONFIG_SCREEN, "screen");
  tos_config_load(TOS_PATH_CONFIG_WIFI,   ASSETS_CONFIG_WIFI,   "wifi");
  tos_config_load(TOS_PATH_CONFIG_BLE,    ASSETS_CONFIG_BLE,    "ble");
  tos_config_load(TOS_PATH_CONFIG_LORA,   ASSETS_CONFIG_LORA,   "lora");
  tos_config_load(TOS_PATH_CONFIG_SYSTEM, ASSETS_CONFIG_SYSTEM, "system");
}

esp_err_t tos_config_save(const char *sd_path, const char *module)
{
  cJSON *root = NULL;

  if (strcmp(module, "screen") == 0)      root = serialize_screen();
  else if (strcmp(module, "wifi") == 0)   root = serialize_wifi();
  else if (strcmp(module, "ble") == 0)    root = serialize_ble();
  else if (strcmp(module, "lora") == 0)   root = serialize_lora();
  else if (strcmp(module, "system") == 0) root = serialize_system();

  if (!root) return ESP_FAIL;

  char *json_str = cJSON_Print(root);
  cJSON_Delete(root);

  if (!json_str) return ESP_ERR_NO_MEM;

  esp_err_t ret = write_string_to_file(sd_path, json_str);
  free(json_str);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "[%s] Saved to %s", module, sd_path);
  } else {
    ESP_LOGE(TAG, "[%s] Failed to save to %s", module, sd_path);
  }

  return ret;
}

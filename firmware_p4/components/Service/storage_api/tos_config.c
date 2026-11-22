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

#include "tos_config.h"
#include "power_manager.h"
#include "haptic.h"
#include "battery_service.h"
#include "power_profile.h"
#include "storage_atomic.h"
#include "tos_storage_paths.h"
#include "esp_log.h"
#include "cJSON.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "TOS_CONFIG";
static atomic_flag s_system_write_busy = ATOMIC_FLAG_INIT;

// Global config instances with defaults

tos_config_screen_t g_config_screen = {
    .brightness = 80,
    .rotation = 1,
    .theme = "default",
    .auto_lock_seconds = 300,
    .auto_dim = true,
};

tos_config_wifi_t g_config_wifi = {
    .enabled = false,
    .ap =
        {
            .ssid = "TentacleOS",
            .password = "12345678",
            .ip_addr = "192.168.4.1",
            .max_conn = 4,
            .enabled = true,
        },
    .client =
        {
            .enabled = false,
            .ssid = "",
            .password = "",
        },
};

tos_config_ble_t g_config_ble = {
    .name = "TentacleOS",
    .max_conn = TOS_CONFIG_BLE_MAX_CLIENTS,
    .advertising = true,
    .enabled = false,
};

tos_config_lora_t g_config_lora = {
    .frequency = 915000000,
    .spreading_factor = 7,
    .bandwidth = 125000,
    .tx_power = 14,
    .sync_word = 0x12,
    .enabled = false,
};

tos_config_system_t g_config_system = {
    .name = "TentacleOS",
    .locale = "pt-BR",
    .timezone_offset = -3,
    .volume = 50,
    .power_saving = true,
    .battery_capacity_mah = POWER_PROFILE_CAPACITY_MAH,
    .battery_profile = BATTERY_SERVICE_DEFAULT_PROFILE,
    .vibration = true,
    .vibration_intensity = HAPTIC_FEEDBACK_INTENSITY,
    .ui_sound_fx = true,
    .log_level = "info",
    .first_boot_done = false,
};

tos_config_led_t g_config_led = {
    .brightness = 10,
    .info_color = 0xFF00FF,    // purple (pure magenta: R+B)
    .warning_color = 0xFFFF00, // yellow
    .error_color = 0xFF0000,   // red
};

// Helpers

static char *read_file_to_string(const char *path) {
  FILE *f = fopen(path, "r");
  if (f == NULL)
    return NULL;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return NULL;
  }

  char *buf = malloc(size + 1);
  if (buf == NULL) {
    fclose(f);
    return NULL;
  }

  fread(buf, 1, size, f);
  buf[size] = '\0';
  fclose(f);
  return buf;
}

static esp_err_t write_string_to_file(const char *path, const char *data) {
  return storage_write_atomic(path, data, strlen(data));
}

static char *recover_from_tmp(const char *path) {
  char tmp[128];
  int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  if (n <= 0 || (size_t)n >= sizeof(tmp))
    return NULL;

  char *content = read_file_to_string(tmp);
  if (content == NULL)
    return NULL;

  cJSON *probe = cJSON_Parse(content);
  if (probe == NULL) {
    free(content);
    remove(tmp);
    return NULL;
  }
  cJSON_Delete(probe);

  remove(path);
  rename(tmp, path);
  return content;
}

static void json_get_str(cJSON *obj, const char *key, char *dst, size_t max) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (cJSON_IsString(item) && item->valuestring) {
    strncpy(dst, item->valuestring, max - 1);
    dst[max - 1] = '\0';
  }
}

static int json_get_int(cJSON *obj, const char *key, int fallback) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool json_get_bool(cJSON *obj, const char *key, bool fallback) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

// Colors are stored as "#RRGGBB" (or "RRGGBB") hex strings for readability.
static uint32_t json_get_color(cJSON *obj, const char *key, uint32_t fallback) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (!cJSON_IsString(item) || item->valuestring == NULL) {
    return fallback;
  }
  const char *s = item->valuestring;
  if (*s == '#') {
    s++;
  }
  return (uint32_t)(strtoul(s, NULL, 16) & 0xFFFFFF);
}

// Parse per module

static void parse_screen(cJSON *root) {
  g_config_screen.brightness = json_get_int(root, "brightness", g_config_screen.brightness);
  g_config_screen.rotation = json_get_int(root, "rotation", g_config_screen.rotation);
  g_config_screen.auto_lock_seconds =
      json_get_int(root, "auto_lock_seconds", g_config_screen.auto_lock_seconds);
  g_config_screen.auto_dim = json_get_bool(root, "auto_dim", g_config_screen.auto_dim);
  json_get_str(root, "theme", g_config_screen.theme, sizeof(g_config_screen.theme));
}

static void parse_wifi(cJSON *root) {
  g_config_wifi.enabled = json_get_bool(root, "enabled", g_config_wifi.enabled);

  cJSON *ap = cJSON_GetObjectItem(root, "ap");
  if (ap == NULL)
    ap = root;
  if (ap) {
    json_get_str(ap, "ssid", g_config_wifi.ap.ssid, sizeof(g_config_wifi.ap.ssid));
    json_get_str(ap, "password", g_config_wifi.ap.password, sizeof(g_config_wifi.ap.password));
    json_get_str(ap, "ip_addr", g_config_wifi.ap.ip_addr, sizeof(g_config_wifi.ap.ip_addr));
    g_config_wifi.ap.max_conn = json_get_int(ap, "max_conn", g_config_wifi.ap.max_conn);
    g_config_wifi.ap.enabled = json_get_bool(ap, "enabled", g_config_wifi.ap.enabled);
  }

  cJSON *client = cJSON_GetObjectItem(root, "client");
  if (client) {
    g_config_wifi.client.enabled = json_get_bool(client, "enabled", g_config_wifi.client.enabled);
    json_get_str(client, "ssid", g_config_wifi.client.ssid, sizeof(g_config_wifi.client.ssid));
    json_get_str(
        client, "password", g_config_wifi.client.password, sizeof(g_config_wifi.client.password));
  }
}

static void parse_ble(cJSON *root) {
  json_get_str(root, "ssid", g_config_ble.name, sizeof(g_config_ble.name));
  json_get_str(root, "name", g_config_ble.name, sizeof(g_config_ble.name));
  int max_conn = json_get_int(root, "max_conn", TOS_CONFIG_BLE_MAX_CLIENTS);
  g_config_ble.max_conn = max_conn >= 1 && max_conn <= TOS_CONFIG_BLE_MAX_CLIENTS
                              ? (uint8_t)max_conn
                              : TOS_CONFIG_BLE_MAX_CLIENTS;
  g_config_ble.advertising = json_get_bool(root, "advertising", true);
  g_config_ble.enabled =
      json_get_bool(root, "enabled_by_user", false) && json_get_bool(root, "enabled", false);
}

static void parse_lora(cJSON *root) {
  g_config_lora.frequency = (uint32_t)json_get_int(root, "frequency", (int)g_config_lora.frequency);
  g_config_lora.spreading_factor =
      json_get_int(root, "spreading_factor", g_config_lora.spreading_factor);
  g_config_lora.bandwidth = (long)json_get_int(root, "bandwidth", (int)g_config_lora.bandwidth);
  g_config_lora.tx_power = json_get_int(root, "tx_power", g_config_lora.tx_power);
  g_config_lora.sync_word = (uint8_t)json_get_int(root, "sync_word", g_config_lora.sync_word);
  g_config_lora.enabled = json_get_bool(root, "enabled", g_config_lora.enabled);
}

static void parse_system(cJSON *root) {
  g_config_system.power_saving = json_get_bool(root, "power_saving", true);
  power_manager_set_saver(g_config_system.power_saving);
  cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "battery_profile");
  const battery_service_profile_t *profile =
      cJSON_IsString(id) ? battery_service_profile_find(id->valuestring) : NULL;
  if (profile == NULL) {
    cJSON *capacity = cJSON_GetObjectItemCaseSensitive(root, "battery_capacity_mah");
    const char *legacy_id =
        cJSON_IsNumber(capacity) && capacity->valuedouble == POWER_PROFILE_CAPACITY_SMALL_MAH
            ? "generic_2000"
            : BATTERY_SERVICE_DEFAULT_PROFILE;
    profile = battery_service_profile_find(legacy_id);
    if (id != NULL)
      ESP_LOGW(TAG, "Unknown battery profile; retaining generic charge guards");
  }
  snprintf(
      g_config_system.battery_profile, sizeof(g_config_system.battery_profile), "%s", profile->id);
  g_config_system.battery_capacity_mah = profile->capacity_mah;
  battery_service_set_profile(profile->id);
  json_get_str(root, "name", g_config_system.name, sizeof(g_config_system.name));
  json_get_str(root, "locale", g_config_system.locale, sizeof(g_config_system.locale));
  json_get_str(root, "log_level", g_config_system.log_level, sizeof(g_config_system.log_level));
  g_config_system.timezone_offset =
      json_get_int(root, "timezone_offset", g_config_system.timezone_offset);
  g_config_system.volume = json_get_int(root, "volume", g_config_system.volume);
  g_config_system.vibration = json_get_bool(root, "vibration", g_config_system.vibration);
  g_config_system.vibration_intensity =
      json_get_int(root, "vibration_intensity", HAPTIC_FEEDBACK_INTENSITY);
  if (g_config_system.vibration_intensity < 20)
    g_config_system.vibration_intensity = 20;
  if (g_config_system.vibration_intensity > 100)
    g_config_system.vibration_intensity = 100;
  haptic_set_feedback_intensity(g_config_system.vibration_intensity);
  g_config_system.ui_sound_fx = json_get_bool(root, "ui_sound_fx", g_config_system.ui_sound_fx);
  g_config_system.first_boot_done =
      json_get_bool(root, "first_boot_done", g_config_system.first_boot_done);
}

static void parse_led(cJSON *root) {
  g_config_led.brightness = json_get_int(root, "brightness", g_config_led.brightness);
  g_config_led.info_color = json_get_color(root, "info_color", g_config_led.info_color);
  g_config_led.warning_color = json_get_color(root, "warning_color", g_config_led.warning_color);
  g_config_led.error_color = json_get_color(root, "error_color", g_config_led.error_color);
}

// Serialize per module

static void json_add_color(cJSON *root, const char *key, uint32_t color) {
  char hex[8];
  snprintf(hex, sizeof(hex), "#%06lX", (unsigned long)(color & 0xFFFFFF));
  cJSON_AddStringToObject(root, key, hex);
}

static cJSON *serialize_led(void) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "brightness", g_config_led.brightness);
  json_add_color(root, "info_color", g_config_led.info_color);
  json_add_color(root, "warning_color", g_config_led.warning_color);
  json_add_color(root, "error_color", g_config_led.error_color);
  return root;
}

static cJSON *serialize_screen(void) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "brightness", g_config_screen.brightness);
  cJSON_AddNumberToObject(root, "rotation", g_config_screen.rotation);
  cJSON_AddStringToObject(root, "theme", g_config_screen.theme);
  cJSON_AddNumberToObject(root, "auto_lock_seconds", g_config_screen.auto_lock_seconds);
  cJSON_AddBoolToObject(root, "auto_dim", g_config_screen.auto_dim);
  return root;
}

static cJSON *serialize_wifi(void) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL || cJSON_AddBoolToObject(root, "enabled", g_config_wifi.enabled) == NULL)
    goto cleanup;
  cJSON *ap = cJSON_AddObjectToObject(root, "ap");
  if (ap == NULL || cJSON_AddStringToObject(ap, "ssid", g_config_wifi.ap.ssid) == NULL ||
      cJSON_AddStringToObject(ap, "password", g_config_wifi.ap.password) == NULL ||
      cJSON_AddStringToObject(ap, "ip_addr", g_config_wifi.ap.ip_addr) == NULL ||
      cJSON_AddNumberToObject(ap, "max_conn", g_config_wifi.ap.max_conn) == NULL ||
      cJSON_AddBoolToObject(ap, "enabled", g_config_wifi.ap.enabled) == NULL)
    goto cleanup;
  cJSON *client = cJSON_AddObjectToObject(root, "client");
  if (client == NULL ||
      cJSON_AddBoolToObject(client, "enabled", g_config_wifi.client.enabled) == NULL ||
      cJSON_AddStringToObject(client, "ssid", g_config_wifi.client.ssid) == NULL ||
      cJSON_AddStringToObject(client, "password", g_config_wifi.client.password) == NULL)
    goto cleanup;
  return root;
cleanup:
  ESP_LOGE(TAG, "Cannot serialize Wi-Fi config");
  cJSON_Delete(root);
  return NULL;
}

static cJSON *serialize_ble(void) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL || cJSON_AddStringToObject(root, "name", g_config_ble.name) == NULL ||
      cJSON_AddNumberToObject(root, "max_conn", g_config_ble.max_conn) == NULL ||
      cJSON_AddBoolToObject(root, "advertising", g_config_ble.advertising) == NULL ||
      cJSON_AddBoolToObject(root, "enabled", g_config_ble.enabled) == NULL ||
      cJSON_AddBoolToObject(root, "enabled_by_user", true) == NULL) {
    ESP_LOGE(TAG, "Cannot serialize Bluetooth config");
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}

static cJSON *serialize_lora(void) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "frequency", g_config_lora.frequency);
  cJSON_AddNumberToObject(root, "spreading_factor", g_config_lora.spreading_factor);
  cJSON_AddNumberToObject(root, "bandwidth", g_config_lora.bandwidth);
  cJSON_AddNumberToObject(root, "tx_power", g_config_lora.tx_power);
  cJSON_AddNumberToObject(root, "sync_word", g_config_lora.sync_word);
  cJSON_AddBoolToObject(root, "enabled", g_config_lora.enabled);
  return root;
}

static cJSON *serialize_system(void) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL || cJSON_AddStringToObject(root, "name", g_config_system.name) == NULL ||
      cJSON_AddStringToObject(root, "locale", g_config_system.locale) == NULL ||
      cJSON_AddNumberToObject(root, "timezone_offset", g_config_system.timezone_offset) == NULL ||
      cJSON_AddNumberToObject(root, "volume", g_config_system.volume) == NULL ||
      cJSON_AddStringToObject(root, "battery_profile", g_config_system.battery_profile) == NULL ||
      cJSON_AddNumberToObject(root, "battery_capacity_mah", g_config_system.battery_capacity_mah) ==
          NULL ||
      cJSON_AddBoolToObject(root, "power_saving", g_config_system.power_saving) == NULL ||
      cJSON_AddBoolToObject(root, "vibration", g_config_system.vibration) == NULL ||
      cJSON_AddNumberToObject(root, "vibration_intensity", g_config_system.vibration_intensity) ==
          NULL ||
      cJSON_AddBoolToObject(root, "ui_sound_fx", g_config_system.ui_sound_fx) == NULL ||
      cJSON_AddStringToObject(root, "log_level", g_config_system.log_level) == NULL ||
      cJSON_AddBoolToObject(root, "first_boot_done", g_config_system.first_boot_done) == NULL)
    goto cleanup;
  return root;
cleanup:
  ESP_LOGE(TAG, "Cannot serialize system config");
  cJSON_Delete(root);
  return NULL;
}

// Public API

static esp_err_t load_config(const char *sd_path, const char *flash_path, const char *module) {
  char *json_str = read_file_to_string(sd_path);
  const char *source = "SD";

  if (json_str == NULL) {
    json_str = recover_from_tmp(sd_path);
    if (json_str != NULL)
      source = "SD (recovered)";
  }

  if (json_str == NULL && flash_path != NULL) {
    json_str = read_file_to_string(flash_path);
    source = "flash";
  }

  if (json_str == NULL) {
    ESP_LOGW(TAG, "[%s] No config found, using defaults", module);
    return ESP_ERR_NOT_FOUND;
  }

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);

  if (root == NULL) {
    ESP_LOGE(TAG, "[%s] JSON parse failed from %s", module, source);
    return ESP_FAIL;
  }

  if (strcmp(module, "screen") == 0)
    parse_screen(root);
  else if (strcmp(module, "wifi") == 0)
    parse_wifi(root);
  else if (strcmp(module, "ble") == 0)
    parse_ble(root);
  else if (strcmp(module, "lora") == 0)
    parse_lora(root);
  else if (strcmp(module, "system") == 0)
    parse_system(root);
  else if (strcmp(module, "led") == 0)
    parse_led(root);

  cJSON_Delete(root);
  ESP_LOGI(TAG, "[%s] Loaded from %s", module, source);
  return ESP_OK;
}

esp_err_t tos_config_load(const char *sd_path, const char *flash_path, const char *module) {
  bool is_system = strcmp(module, "system") == 0;
  if (is_system && atomic_flag_test_and_set_explicit(&s_system_write_busy, memory_order_acquire))
    return ESP_ERR_INVALID_STATE;
  esp_err_t result = load_config(sd_path, flash_path, module);
  if (is_system)
    atomic_flag_clear_explicit(&s_system_write_busy, memory_order_release);
  return result;
}

void tos_config_load_all(void) {
  tos_config_load(TOS_PATH_CONFIG_SCREEN, ASSETS_CONFIG_SCREEN, "screen");
  tos_config_load(TOS_PATH_CONFIG_WIFI, ASSETS_CONFIG_WIFI, "wifi");
  tos_config_load(TOS_PATH_CONFIG_BLE, ASSETS_CONFIG_BLE, "ble");
  tos_config_load(TOS_PATH_CONFIG_LORA, ASSETS_CONFIG_LORA, "lora");
  tos_config_load(TOS_PATH_CONFIG_SYSTEM, ASSETS_CONFIG_SYSTEM, "system");
  tos_config_load(TOS_PATH_CONFIG_LED, ASSETS_CONFIG_LED, "led");
}

static esp_err_t save_config(const char *sd_path, const char *module) {
  cJSON *root = NULL;

  if (strcmp(module, "screen") == 0)
    root = serialize_screen();
  else if (strcmp(module, "wifi") == 0)
    root = serialize_wifi();
  else if (strcmp(module, "ble") == 0)
    root = serialize_ble();
  else if (strcmp(module, "lora") == 0)
    root = serialize_lora();
  else if (strcmp(module, "system") == 0)
    root = serialize_system();
  else if (strcmp(module, "led") == 0)
    root = serialize_led();

  if (root == NULL)
    return ESP_FAIL;

  char *json_str = cJSON_Print(root);
  cJSON_Delete(root);

  if (json_str == NULL)
    return ESP_ERR_NO_MEM;

  esp_err_t ret = write_string_to_file(sd_path, json_str);
  free(json_str);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "[%s] Saved to %s", module, sd_path);
  } else {
    ESP_LOGE(TAG, "[%s] Failed to save to %s", module, sd_path);
  }

  return ret;
}

esp_err_t tos_config_save(const char *sd_path, const char *module) {
  bool is_system = strcmp(module, "system") == 0;
  if (is_system && atomic_flag_test_and_set_explicit(&s_system_write_busy, memory_order_acquire))
    return ESP_ERR_INVALID_STATE;
  esp_err_t result = save_config(sd_path, module);
  if (is_system)
    atomic_flag_clear_explicit(&s_system_write_busy, memory_order_release);
  return result;
}

esp_err_t tos_config_set_power_saving(bool enabled) {
  if (atomic_flag_test_and_set_explicit(&s_system_write_busy, memory_order_acquire))
    return ESP_ERR_INVALID_STATE;
  bool previous = g_config_system.power_saving;
  g_config_system.power_saving = enabled;
  esp_err_t result = save_config(TOS_PATH_CONFIG_SYSTEM, "system");
  if (result == ESP_OK)
    power_manager_set_saver(enabled);
  else
    g_config_system.power_saving = previous;
  atomic_flag_clear_explicit(&s_system_write_busy, memory_order_release);
  return result;
}

esp_err_t tos_config_set_battery_profile(const char *id) {
  const battery_service_profile_t *profile = battery_service_profile_find(id);
  if (profile == NULL)
    return ESP_ERR_INVALID_ARG;
  if (atomic_flag_test_and_set_explicit(&s_system_write_busy, memory_order_acquire))
    return ESP_ERR_INVALID_STATE;
  char previous_id[sizeof(g_config_system.battery_profile)];
  memcpy(previous_id, g_config_system.battery_profile, sizeof(previous_id));
  uint16_t previous_mah = g_config_system.battery_capacity_mah;
  snprintf(
      g_config_system.battery_profile, sizeof(g_config_system.battery_profile), "%s", profile->id);
  g_config_system.battery_capacity_mah = profile->capacity_mah;
  esp_err_t result = save_config(TOS_PATH_CONFIG_SYSTEM, "system");
  if (result != ESP_OK) {
    memcpy(g_config_system.battery_profile, previous_id, sizeof(previous_id));
    g_config_system.battery_capacity_mah = previous_mah;
  } else {
    result = battery_service_set_profile(profile->id);
  }
  atomic_flag_clear_explicit(&s_system_write_busy, memory_order_release);
  return result;
}

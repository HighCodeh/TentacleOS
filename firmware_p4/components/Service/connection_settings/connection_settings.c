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

#include "connection_settings.h"

#include <string.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "bluetooth_service.h"
#include "led_control.h"
#include "sys_prio.h"
#include "tos_storage_paths.h"
#include "wifi_service.h"

#define SETTINGS_STACK_SIZE 4096
#define WIFI_SSID_MAX       32

static const char *TAG = "CONNECTION_SETTINGS";

typedef struct {
  connection_settings_kind_t kind;
  union {
    tos_config_wifi_t wifi;
    tos_config_ble_t ble;
  } config;
} connection_settings_request_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static connection_settings_status_t s_status;

static esp_err_t request(connection_settings_kind_t kind, const void *config, size_t size);
static void apply_task(void *argument);
static esp_err_t apply_wifi(const tos_config_wifi_t *config);
static esp_err_t apply_ble(const tos_config_ble_t *config);

esp_err_t connection_settings_validate_wifi(const tos_config_wifi_t *config) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  size_t name_len = strnlen(config->ap.ssid, sizeof(config->ap.ssid));
  size_t password_len = strnlen(config->ap.password, sizeof(config->ap.password));
  if (name_len == 0 || name_len > WIFI_SSID_MAX ||
      (password_len != 0 && (password_len < CONNECTION_SETTINGS_PASSWORD_MIN ||
                             password_len > CONNECTION_SETTINGS_PASSWORD_MAX)) ||
      config->ap.max_conn < 1 || config->ap.max_conn > CONNECTION_SETTINGS_WIFI_MAX_CLIENTS)
    return ESP_ERR_INVALID_ARG;
  return ESP_OK;
}

esp_err_t connection_settings_validate_ble(const tos_config_ble_t *config) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  size_t length = strnlen(config->name, sizeof(config->name));
  if (length == 0 || length > CONNECTION_SETTINGS_NAME_MAX || config->max_conn < 1 ||
      config->max_conn > CONNECTION_SETTINGS_BLE_MAX_CLIENTS)
    return ESP_ERR_INVALID_ARG;
  return ESP_OK;
}

esp_err_t connection_settings_request_wifi(const tos_config_wifi_t *config) {
  esp_err_t error = connection_settings_validate_wifi(config);
  return error == ESP_OK ? request(CONNECTION_SETTINGS_WIFI, config, sizeof(*config)) : error;
}

esp_err_t connection_settings_request_ble(const tos_config_ble_t *config) {
  esp_err_t error = connection_settings_validate_ble(config);
  return error == ESP_OK ? request(CONNECTION_SETTINGS_BLE, config, sizeof(*config)) : error;
}

void connection_settings_get_status(connection_settings_status_t *out_status) {
  if (out_status == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out_status = s_status;
  portEXIT_CRITICAL(&s_lock);
}

void connection_settings_get_config(tos_config_wifi_t *out_wifi, tos_config_ble_t *out_ble) {
  portENTER_CRITICAL(&s_lock);
  if (out_wifi != NULL)
    *out_wifi = g_config_wifi;
  if (out_ble != NULL)
    *out_ble = g_config_ble;
  portEXIT_CRITICAL(&s_lock);
}

static esp_err_t request(connection_settings_kind_t kind, const void *config, size_t size) {
  portENTER_CRITICAL(&s_lock);
  if (s_status.is_busy) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_status.is_busy = true;
  s_status.has_result = false;
  s_status.is_applied = false;
  s_status.kind = kind;
  portEXIT_CRITICAL(&s_lock);
  esp_err_t error = ESP_ERR_NO_MEM;
  connection_settings_request_t *pending =
      heap_caps_calloc(1, sizeof(*pending), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pending == NULL) {
    ESP_LOGE(TAG, "Cannot allocate settings request");
    goto cleanup;
  }
  pending->kind = kind;
  memcpy(&pending->config, config, size);
  if (xTaskCreatePinnedToCoreWithCaps(apply_task,
                                      "radio_settings",
                                      SETTINGS_STACK_SIZE,
                                      pending,
                                      SYS_PRIO_SERVICE_LO,
                                      NULL,
                                      SYS_CORE_RADIO,
                                      MALLOC_CAP_SPIRAM) != pdPASS) {
    ESP_LOGE(TAG, "Cannot create settings worker");
    goto cleanup;
  }
  return ESP_OK;
cleanup:
  free(pending);
  portENTER_CRITICAL(&s_lock);
  s_status.is_busy = false;
  s_status.has_result = true;
  s_status.error = error;
  ++s_status.revision;
  portEXIT_CRITICAL(&s_lock);
  led_signal_error();
  return error;
}

static esp_err_t apply_wifi(const tos_config_wifi_t *config) {
  return wifi_service_save_ap_config(config->ap.ssid,
                                     config->ap.password,
                                     (uint8_t)config->ap.max_conn,
                                     config->ap.ip_addr,
                                     config->enabled);
}

static esp_err_t apply_ble(const tos_config_ble_t *config) {
  esp_err_t error = bluetooth_service_deinit();
  if (error == ESP_OK)
    error = bluetooth_service_save_announce_config(config->name, config->max_conn);
  if (error == ESP_OK && config->enabled)
    error = bluetooth_service_start();
  if (error == ESP_OK && config->enabled && config->advertising)
    error = bluetooth_service_start_advertising();
  if (error != ESP_OK && config->enabled)
    (void)bluetooth_service_deinit();
  return error;
}

static void apply_task(void *argument) {
  connection_settings_request_t *pending = argument;
  bool is_wifi = pending->kind == CONNECTION_SETTINGS_WIFI;
  esp_err_t error = is_wifi ? apply_wifi(&pending->config.wifi) : apply_ble(&pending->config.ble);
  bool is_applied = error == ESP_OK;
  if (is_applied) {
    portENTER_CRITICAL(&s_lock);
    if (is_wifi) {
      g_config_wifi = pending->config.wifi;
      g_config_wifi.ap.enabled = g_config_wifi.enabled;
    } else {
      g_config_ble = pending->config.ble;
    }
    portEXIT_CRITICAL(&s_lock);
    error = tos_config_save(is_wifi ? TOS_PATH_CONFIG_WIFI : TOS_PATH_CONFIG_BLE,
                            is_wifi ? "wifi" : "ble");
  }
  memset(&pending->config, 0, sizeof(pending->config));
  free(pending);
  if (error == ESP_OK) {
    ESP_LOGI(TAG, "%s settings applied and saved", is_wifi ? "Wi-Fi" : "Bluetooth");
    led_signal_info();
  } else {
    ESP_LOGE(TAG,
             "%s settings %s: %s",
             is_wifi ? "Wi-Fi" : "Bluetooth",
             is_applied ? "save failed" : "apply failed",
             esp_err_to_name(error));
    led_signal_error();
  }
  portENTER_CRITICAL(&s_lock);
  s_status.error = error;
  s_status.is_applied = is_applied;
  s_status.has_result = true;
  ++s_status.revision;
  s_status.is_busy = false;
  portEXIT_CRITICAL(&s_lock);
  vTaskDeleteWithCaps(NULL);
}

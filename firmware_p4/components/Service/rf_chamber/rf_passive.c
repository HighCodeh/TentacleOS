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

#include "rf_passive.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "led_control.h"
#include "power_manager.h"

#define RF_PASSIVE_PATH_MIN 1U
#define RF_PASSIVE_PATH_MAX 4U
#define LOCK_WAIT_MS        200U
#define NVS_NAMESPACE       "rf_lab"
#define NVS_BOOT_KEY        "passive"
static const char *TAG = "RF_PASSIVE";
static bool s_is_boot;
static esp_err_t s_save_error;
static rf_passive_status_t s_status = {.path = RF_PASSIVE_PATH_MIN};
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

esp_err_t rf_passive_load(void) {
  s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
  nvs_handle_t handle;
  uint8_t mode = 0;
  esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (error == ESP_ERR_NVS_NOT_FOUND)
    return ESP_OK;
  if (error == ESP_OK) {
    error = nvs_get_u8(handle, NVS_BOOT_KEY, &mode);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND)
      error = ESP_OK;
  }
  if (mode > RF_PASSIVE_PATH_MAX)
    error = ESP_ERR_INVALID_STATE;
  s_is_boot = mode != 0 || error != ESP_OK;
  s_status.path =
      mode >= RF_PASSIVE_PATH_MIN && mode <= RF_PASSIVE_PATH_MAX ? mode : RF_PASSIVE_PATH_MIN;
  s_status.error = error;
  return error;
}
bool rf_passive_is_boot(void) {
  return s_is_boot;
}
void rf_passive_startup(void) {
  if (!s_is_boot)
    return;
  rf_lab_hw_quiet(&s_status.hardware);
  esp_err_t error = power_manager_no_sleep_acquire();
  if (error == ESP_OK)
    error = rf_lab_hw_path_set(s_status.path);
  const esp_err_t results[] = {s_status.error,
                               s_status.hardware.c5,
                               s_status.hardware.cc,
                               s_status.hardware.sx,
                               s_status.hardware.nfc,
                               s_status.hardware.lf};
  for (unsigned i = 0; i < sizeof(results) / sizeof(results[0]); i++)
    if (error == ESP_OK)
      error = results[i];
  s_status.error = error;
  ESP_LOGI(TAG,
           "Passive boot path=RF%u status=%s; C5 held, no radio services",
           s_status.path,
           esp_err_to_name(error));
}
esp_err_t rf_passive_save_boot(bool enabled, uint8_t path) {
  if (path < RF_PASSIVE_PATH_MIN || path > RF_PASSIVE_PATH_MAX)
    return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (error != ESP_OK)
    return error;
  error = nvs_set_u8(handle, NVS_BOOT_KEY, enabled ? path : 0);
  if (error == ESP_OK)
    error = nvs_commit(handle);
  nvs_close(handle);
  return error;
}
esp_err_t rf_passive_select(uint8_t path) {
  if (!s_is_boot || s_mutex == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(LOCK_WAIT_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t error = rf_lab_hw_path_set(path);
  if (error == ESP_OK) {
    s_status.path = path;
    error = rf_passive_save_boot(true, path);
    s_save_error = error;
  }
  xSemaphoreGive(s_mutex);
  if (error == ESP_OK) {
    ESP_LOGI(TAG, "RF%u selected and saved", path);
    led_signal_info();
  } else {
    ESP_LOGE(TAG, "Selection/save failed: %s", esp_err_to_name(error));
    led_signal_error();
  }
  return error;
}
void rf_passive_snapshot(rf_passive_status_t *out_status) {
  if (out_status == NULL || s_mutex == NULL)
    return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out_status = s_status;
  if (out_status->error == ESP_OK)
    out_status->error = s_save_error;
  uint8_t actual = 0;
  if (s_is_boot && rf_lab_hw_path_get(&actual) == ESP_OK && actual != s_status.path)
    out_status->error = ESP_ERR_INVALID_RESPONSE;
  xSemaphoreGive(s_mutex);
}

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

#include "power_manager.h"

#include <stdint.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

#define PM_FREQ_MHZ 360

static const char *TAG = "POWER_MGR";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_external_power;
static bool s_usb_suspended;
#if CONFIG_PM_ENABLE
static bool s_usb_hold;
static bool s_is_initializing;
static esp_pm_lock_handle_t s_no_sleep_lock;
static esp_pm_lock_handle_t s_pending_lock;
static bool s_pending_held;
static uint32_t s_client_holds;
#endif

static esp_err_t update_usb_hold(void) {
#if CONFIG_PM_ENABLE
  if (s_no_sleep_lock == NULL)
    return ESP_OK;
  bool want = s_external_power && !s_usb_suspended;
  if (want == s_usb_hold)
    return ESP_OK;
  esp_err_t error =
      want ? esp_pm_lock_acquire(s_no_sleep_lock) : esp_pm_lock_release(s_no_sleep_lock);
  if (error == ESP_OK)
    s_usb_hold = want;
  return error;
#else
  return ESP_OK;
#endif
}

void power_manager_set_external_power(bool present) {
  portENTER_CRITICAL(&s_state_lock);
  s_external_power = present;
  esp_err_t error = update_usb_hold();
  portEXIT_CRITICAL(&s_state_lock);
  if (error != ESP_OK)
    ESP_LOGW(TAG, "External-power hold update failed: %s", esp_err_to_name(error));
}

void power_manager_set_usb_suspended(bool suspended) {
  portENTER_CRITICAL(&s_state_lock);
  s_usb_suspended = suspended;
  esp_err_t error = update_usb_hold();
  portEXIT_CRITICAL(&s_state_lock);
  if (error != ESP_OK)
    ESP_LOGW(TAG, "USB hold update failed: %s", esp_err_to_name(error));
}

bool power_manager_external_power(void) {
  portENTER_CRITICAL(&s_state_lock);
  bool present = s_external_power;
  portEXIT_CRITICAL(&s_state_lock);
  return present;
}

#if CONFIG_PM_ENABLE
static esp_err_t cleanup_pending(void) {
  if (s_pending_lock == NULL)
    return ESP_OK;
  if (s_pending_held) {
    esp_err_t error = esp_pm_lock_release(s_pending_lock);
    if (error != ESP_OK)
      return error;
    s_pending_held = false;
  }
  esp_err_t error = esp_pm_lock_delete(s_pending_lock);
  if (error == ESP_OK)
    s_pending_lock = NULL;
  return error;
}
#endif

void power_manager_init(void) {
#if CONFIG_PM_ENABLE
  portENTER_CRITICAL(&s_state_lock);
  if (s_no_sleep_lock != NULL || s_is_initializing) {
    portEXIT_CRITICAL(&s_state_lock);
    return;
  }
  s_is_initializing = true;
  portEXIT_CRITICAL(&s_state_lock);

  esp_err_t error = cleanup_pending();
  if (error != ESP_OK)
    goto cleanup;
  error = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_sleep", &s_pending_lock);
  if (error != ESP_OK)
    goto cleanup;
  error = esp_pm_lock_acquire(s_pending_lock);
  if (error != ESP_OK)
    goto cleanup;
  s_pending_held = true;
  const esp_pm_config_t config = {
      .max_freq_mhz = PM_FREQ_MHZ, .min_freq_mhz = PM_FREQ_MHZ, .light_sleep_enable = true};
  error = esp_pm_configure(&config);
  if (error != ESP_OK)
    goto cleanup;

  portENTER_CRITICAL(&s_state_lock);
  s_no_sleep_lock = s_pending_lock;
  s_pending_lock = NULL;
  s_pending_held = false;
  s_usb_hold = true;
  error = update_usb_hold();
  s_is_initializing = false;
  portEXIT_CRITICAL(&s_state_lock);
  if (error != ESP_OK)
    ESP_LOGW(TAG, "Initial wake hold retained: %s", esp_err_to_name(error));
  ESP_LOGI(TAG, "Power manager: light sleep on, %d MHz pinned", PM_FREQ_MHZ);
  return;
cleanup:
  if (s_pending_lock != NULL) {
    esp_err_t cleanup_error = cleanup_pending();
    if (cleanup_error != ESP_OK)
      ESP_LOGE(TAG, "Power initialization cleanup retained: %s", esp_err_to_name(cleanup_error));
  }
  portENTER_CRITICAL(&s_state_lock);
  s_is_initializing = false;
  portEXIT_CRITICAL(&s_state_lock);
  ESP_LOGE(TAG, "Power initialization failed: %s", esp_err_to_name(error));
#endif
}

esp_err_t power_manager_no_sleep_acquire(void) {
#if CONFIG_PM_ENABLE
  portENTER_CRITICAL(&s_state_lock);
  esp_err_t error = ESP_ERR_INVALID_STATE;
  if (s_no_sleep_lock != NULL && s_client_holds != UINT32_MAX) {
    error = esp_pm_lock_acquire(s_no_sleep_lock);
    if (error == ESP_OK)
      s_client_holds++;
  }
  portEXIT_CRITICAL(&s_state_lock);
  return error;
#else
  return ESP_OK;
#endif
}

esp_err_t power_manager_no_sleep_release(void) {
#if CONFIG_PM_ENABLE
  portENTER_CRITICAL(&s_state_lock);
  esp_err_t error = ESP_ERR_INVALID_STATE;
  if (s_no_sleep_lock != NULL && s_client_holds != 0) {
    error = esp_pm_lock_release(s_no_sleep_lock);
    if (error == ESP_OK)
      s_client_holds--;
  }
  portEXIT_CRITICAL(&s_state_lock);
  return error;
#else
  return ESP_OK;
#endif
}

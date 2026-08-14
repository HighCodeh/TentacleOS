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

#include "esp_log.h"
#include "esp_pm.h"

static const char *TAG = "POWER_MGR";

// P4 max is 360 MHz; drop to the XTAL (40 MHz) when idle. Light sleep is enabled
// but gated by the NO_LIGHT_SLEEP lock (held while the screen is on / a resource
// is active), so it only engages when the device is genuinely idle.
#define PM_MAX_FREQ_MHZ 360
#define PM_MIN_FREQ_MHZ 40

static esp_pm_lock_handle_t s_no_sleep_lock = NULL;

void power_manager_init(void) {
  esp_pm_config_t cfg = {
      .max_freq_mhz = PM_MAX_FREQ_MHZ,
      .min_freq_mhz = PM_MIN_FREQ_MHZ,
      .light_sleep_enable = true,
  };
  esp_err_t err = esp_pm_configure(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    return;
  }

  err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_sleep", &s_no_sleep_lock);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(err));
    s_no_sleep_lock = NULL;
    return;
  }

  ESP_LOGI(TAG, "Power manager: DFS %d-%d MHz, light sleep gated by NO_LIGHT_SLEEP lock",
           PM_MIN_FREQ_MHZ, PM_MAX_FREQ_MHZ);
}

esp_err_t power_manager_no_sleep_acquire(void) {
  if (s_no_sleep_lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  return esp_pm_lock_acquire(s_no_sleep_lock);
}

esp_err_t power_manager_no_sleep_release(void) {
  if (s_no_sleep_lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  return esp_pm_lock_release(s_no_sleep_lock);
}

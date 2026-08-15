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

#include "sdkconfig.h"

// USB / external-power state (item 41). Tracked regardless of CONFIG_PM_ENABLE
// so power_manager_external_power() is always valid for policy; the wake hold it
// derives is a no-op until PM is enabled. Hold the CPU out of light sleep while
// plugged in AND the host has not suspended the bus, so it never sleeps while
// charging or mid host-link / OTA session.
static bool s_external_power = false;
static bool s_usb_suspended = false;
static bool s_usb_hold = false;

static void usb_update_hold(void) {
  bool want = s_external_power && !s_usb_suspended;
  if (want == s_usb_hold) {
    return;
  }
  s_usb_hold = want;
  if (want) {
    power_manager_no_sleep_acquire();
  } else {
    power_manager_no_sleep_release();
  }
}

void power_manager_set_external_power(bool present) {
  if (present == s_external_power) {
    return;
  }
  s_external_power = present;
  usb_update_hold();
}

void power_manager_set_usb_suspended(bool suspended) {
  if (suspended == s_usb_suspended) {
    return;
  }
  s_usb_suspended = suspended;
  usb_update_hold();
}

bool power_manager_external_power(void) {
  return s_external_power;
}

#if CONFIG_PM_ENABLE

#include "esp_log.h"
#include "esp_pm.h"

static const char *TAG = "POWER_MGR";

// Frequency is PINNED (min == max): no DFS. DFS would scale the APB and break the
// console UART baud (the IDF UART driver excludes the console UART from PM). The
// power win here is light sleep (CPU fully off when idle), not DFS. Light sleep
// is gated by the NO_LIGHT_SLEEP lock, held while the screen is on, native USB
// is connected, or external power is present (item 41).
#define PM_FREQ_MHZ 360

static esp_pm_lock_handle_t s_no_sleep_lock = NULL;

void power_manager_init(void) {
  esp_pm_config_t cfg = {
      .max_freq_mhz = PM_FREQ_MHZ,
      .min_freq_mhz = PM_FREQ_MHZ,  // == max: no DFS, keeps the console UART baud
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

  ESP_LOGI(TAG, "Power manager: light sleep on (no DFS, %d MHz pinned)", PM_FREQ_MHZ);
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

#else // !CONFIG_PM_ENABLE

// PM disabled: light sleep breaks the console UART + USB until the resource-lock
// discipline lands. Everything is an inert no-op; the CPU stays at its fixed
// frequency and never sleeps. The USB state above is still tracked for policy.
void power_manager_init(void) {}
esp_err_t power_manager_no_sleep_acquire(void) { return ESP_OK; }
esp_err_t power_manager_no_sleep_release(void) { return ESP_OK; }

#endif // CONFIG_PM_ENABLE

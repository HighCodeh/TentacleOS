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

#include "subghz_settings.h"
#include "frequency_catalog.h"

#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#define DEFAULT_FREQUENCY_HZ 433920000U

static const char *TAG = "SUBGHZ_SETTINGS";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static subghz_settings_t s_settings = {
    .preset = CC1101_PRESET_OOK_270KHZ,
    .freq = DEFAULT_FREQUENCY_HZ,
};

static void lock(void);
static void unlock(void);

const uint32_t *subghz_settings_get_frequencies(size_t *out_count) {
  return frequency_catalog_get(out_count);
}

subghz_settings_t subghz_settings_get(void) {
  lock();
  subghz_settings_t settings = s_settings;
  unlock();
  if (!settings.has_custom_radio)
    cc1101_config_from_preset(settings.preset, &settings.radio);
  return settings;
}

void subghz_settings_set_preset(cc1101_preset_t preset) {
  cc1101_radio_config_t radio;
  if (cc1101_config_from_preset(preset, &radio) != ESP_OK) {
    ESP_LOGE(TAG, "Invalid radio preset");
    return;
  }
  lock();
  s_settings.preset = preset;
  s_settings.radio = radio;
  s_settings.has_custom_radio = false;
  unlock();
}

void subghz_settings_set_freq(uint32_t freq) {
  lock();
  s_settings.freq = freq;
  unlock();
}

esp_err_t subghz_settings_set_radio(const cc1101_radio_config_t *radio) {
  if (cc1101_config_validate(radio) != ESP_OK)
    return ESP_ERR_INVALID_ARG;
  bool is_custom = cc1101_config_is_custom(radio);
  lock();
  s_settings.preset = radio->preset;
  s_settings.radio = *radio;
  s_settings.has_custom_radio = is_custom;
  unlock();
  return ESP_OK;
}

static void lock(void) {
  portENTER_CRITICAL(&s_lock);
}

static void unlock(void) {
  portEXIT_CRITICAL(&s_lock);
}

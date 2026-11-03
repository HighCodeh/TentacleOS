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

#include "board_pins.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"

static const char *TAG = "BOARD_PINS";

#define BOARD_SAFE_OUTPUT_LEVEL 0
#define BOARD_C5_RESET_ASSERTED 1
#define BOARD_C5_BOOT_ASSERTED  1
#define BOARD_C5_BOOT_SETTLE_MS 10
#define BOARD_C5_RESET_HOLD_MS  20

static const gpio_num_t SAFE_OUTPUTS[] = {
    GPIO_C5_RESET_PIN, GPIO_C5_BOOT_PIN, GPIO_USB_MUX_SEL_PIN, GPIO_CHARGER_OTG_PIN};

esp_err_t board_pins_init(void) {
  for (size_t i = 0; i < sizeof(SAFE_OUTPUTS) / sizeof(SAFE_OUTPUTS[0]); i++) {
    // Preload the latch before enabling output to avoid an assertion pulse.
    esp_err_t ret = gpio_set_level(SAFE_OUTPUTS[i], BOARD_SAFE_OUTPUT_LEVEL);
    if (ret == ESP_OK) {
      const gpio_config_t config = {
          .pin_bit_mask = 1ULL << SAFE_OUTPUTS[i],
          .mode = GPIO_MODE_OUTPUT,
          .pull_up_en = GPIO_PULLUP_DISABLE,
          .pull_down_en = GPIO_PULLDOWN_DISABLE,
          .intr_type = GPIO_INTR_DISABLE,
      };
      ret = gpio_config(&config);
    }
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Safe GPIO %d failed: %s", SAFE_OUTPUTS[i], esp_err_to_name(ret));
      return ret;
    }
  }
  ESP_LOGI(TAG, "High Boy V2.03 safe outputs initialized");
  return ESP_OK;
}

esp_err_t board_pins_reset_c5(void) {
  // T13/T14 invert the P4 controls; zero releases both C5 EN and BOOT.
  esp_err_t ret = gpio_set_level(GPIO_C5_BOOT_PIN, BOARD_SAFE_OUTPUT_LEVEL);
  if (ret != ESP_OK)
    return ret;
  vTaskDelay(pdMS_TO_TICKS(BOARD_C5_BOOT_SETTLE_MS));
  ret = gpio_set_level(GPIO_C5_RESET_PIN, BOARD_C5_RESET_ASSERTED);
  if (ret == ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(BOARD_C5_RESET_HOLD_MS));
  esp_err_t release = gpio_set_level(GPIO_C5_RESET_PIN, BOARD_SAFE_OUTPUT_LEVEL);
  if (release != ESP_OK)
    return release;
  return ret;
}

esp_err_t board_pins_download_c5(void) {
  // Keep T14 asserted through external programmer resets until P4 restarts.
  esp_err_t ret = gpio_set_level(GPIO_C5_BOOT_PIN, BOARD_C5_BOOT_ASSERTED);
  if (ret == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(BOARD_C5_BOOT_SETTLE_MS));
    ret = gpio_set_level(GPIO_C5_RESET_PIN, BOARD_C5_RESET_ASSERTED);
    if (ret == ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(BOARD_C5_RESET_HOLD_MS));
  }
  esp_err_t release = gpio_set_level(GPIO_C5_RESET_PIN, BOARD_SAFE_OUTPUT_LEVEL);
  if (release != ESP_OK)
    ret = release;
  if (ret != ESP_OK) {
    esp_err_t boot_release = gpio_set_level(GPIO_C5_BOOT_PIN, BOARD_SAFE_OUTPUT_LEVEL);
    if (boot_release != ESP_OK)
      ret = boot_release;
    ESP_LOGE(TAG, "C5 download GPIO sequence failed: %s", esp_err_to_name(ret));
  }
  return ret;
}

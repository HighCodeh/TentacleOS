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
#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_log.h"

#include "pin_def.h"

static atomic_bool s_haptic_irq_pending;

static void haptic_interrupt(void *arg) {
  (void)arg;
  atomic_store_explicit(&s_haptic_irq_pending, true, memory_order_relaxed);
}

static const char *TAG = "BOARD_PINS";

#define BOARD_SAFE_OUTPUT_LEVEL 0

static const gpio_num_t SAFE_OUTPUTS[] = {
    GPIO_AUDIO_EN_PIN, GPIO_HAPTIC_RST_PIN, GPIO_USB_MUX_OE_PIN, GPIO_IR_TX_PIN};

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
  // INTN is open-drain; sheet 6 has no external pull-up on this net.
  const gpio_config_t input = {
      .pin_bit_mask = 1ULL << GPIO_HAPTIC_INT_PIN,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t ret = gpio_config(&input);
  if (ret != ESP_OK)
    return ret;
  ret = gpio_install_isr_service(0);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    return ret;
  ret = gpio_isr_handler_add(GPIO_HAPTIC_INT_PIN, haptic_interrupt, NULL);
  if (ret != ESP_OK)
    return ret;
  ret = gpio_set_intr_type(GPIO_HAPTIC_INT_PIN, GPIO_INTR_NEGEDGE);
  if (ret == ESP_OK)
    ret = gpio_intr_enable(GPIO_HAPTIC_INT_PIN);
  if (ret != ESP_OK) {
    gpio_isr_handler_remove(GPIO_HAPTIC_INT_PIN);
    return ret;
  }
  ESP_LOGI(TAG, "High Boy V2.03 safe outputs initialized");
  return ESP_OK;
}

esp_err_t board_pins_set_amplifier(bool enabled) {
  return gpio_set_level(GPIO_AUDIO_EN_PIN, enabled ? 1 : 0);
}

esp_err_t board_pins_set_haptic_release(bool released) {
  esp_err_t result = gpio_set_level(GPIO_HAPTIC_RST_PIN, released ? 1 : 0);
  if (result == ESP_OK && !released)
    atomic_store(&s_haptic_irq_pending, false);
  return result;
}

esp_err_t board_pins_set_usb_enable(bool enabled) {
  return gpio_set_level(GPIO_USB_MUX_OE_PIN, enabled ? 0 : 1);
}

bool board_pins_haptic_int_high(void) {
  return gpio_get_level(GPIO_HAPTIC_INT_PIN) != 0;
}

bool board_pins_haptic_irq_pending(void) {
  return atomic_load_explicit(&s_haptic_irq_pending, memory_order_relaxed);
}

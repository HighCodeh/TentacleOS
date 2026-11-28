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

#include "header_spi.h"

#include <string.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "spi.h"
#include "pin_def.h"
#include "power_manager.h"

#define HEADER_SPI_CLOCK_MIN   100000U
#define HEADER_SPI_CLOCK_MAX   4000000U
#define HEADER_SPI_MODE_MAX    3U
#define HEADER_SPI_LOCK_MS     100U
#define HEADER_SPI_QUEUE_SIZE  1
#define HEADER_SPI_SOFTWARE_CS -1
#define BITS_PER_BYTE          8U

static const char *TAG = "HEADER_SPI";
static spi_device_handle_t s_device;
static header_spi_config_t s_config;
static bool s_has_sleep_hold;
/* Small internal DMA staging buffers; the app's PSRAM pointers never reach DMA. */
DMA_ATTR static uint8_t s_tx[HEADER_SPI_TRANSFER_MAX];
DMA_ATTR static uint8_t s_rx[HEADER_SPI_TRANSFER_MAX];

static int pin_gpio(uint8_t pin) {
  if (pin == HEADER_SPI_DEBUG_DM_PIN)
    return GPIO_USB_DEBUG_DM_PIN;
  if (pin == HEADER_SPI_DEBUG_DP_PIN)
    return GPIO_USB_DEBUG_DP_PIN;
  return -1;
}

static esp_err_t configure_pin(int pin, bool output, bool high) {
  esp_err_t error = gpio_set_level(pin, high);
  if (error != ESP_OK)
    return error;
  gpio_config_t config = {
      .pin_bit_mask = 1ULL << pin,
      .mode = output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
      .pull_up_en = high ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
      .pull_down_en = high ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  return gpio_config(&config);
}

esp_err_t header_spi_open(const header_spi_config_t *config) {
  if (!config || pin_gpio(config->cs_pin) < 0 || pin_gpio(config->control_pin) < 0 ||
      config->cs_pin == config->control_pin || config->mode > HEADER_SPI_MODE_MAX ||
      config->clock_hz < HEADER_SPI_CLOCK_MIN || config->clock_hz > HEADER_SPI_CLOCK_MAX)
    return ESP_ERR_INVALID_ARG;
  if (s_device || s_has_sleep_hold)
    return ESP_ERR_INVALID_STATE;
  if (!spi_bus_lock_take(HEADER_SPI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error = power_manager_no_sleep_acquire();
  if (error != ESP_OK) {
    spi_bus_lock_give();
    return error;
  }
  s_has_sleep_hold = true;
  error = configure_pin(pin_gpio(config->cs_pin), true, true);
  if (error != ESP_OK)
    goto cleanup;
  error = configure_pin(pin_gpio(config->control_pin), true, false);
  if (error != ESP_OK)
    goto cleanup;
  spi_device_interface_config_t device = {
      .clock_speed_hz = config->clock_hz,
      .mode = config->mode,
      .spics_io_num = HEADER_SPI_SOFTWARE_CS,
      .queue_size = HEADER_SPI_QUEUE_SIZE,
  };
  error = spi_bus_add_device(SPI3_HOST, &device, &s_device);
  if (error == ESP_OK) {
    s_config = *config;
    ESP_LOGI(TAG,
             "Header SPI ready: CS pin %u, control pin %u, %lu Hz",
             config->cs_pin,
             config->control_pin,
             (unsigned long)config->clock_hz);
  }
cleanup:
  if (error != ESP_OK) {
    configure_pin(pin_gpio(config->control_pin), false, false);
    configure_pin(pin_gpio(config->cs_pin), false, true);
    if (power_manager_no_sleep_release() == ESP_OK)
      s_has_sleep_hold = false;
    ESP_LOGE(TAG, "Header SPI open failed: %s", esp_err_to_name(error));
  }
  spi_bus_lock_give();
  return error;
}

esp_err_t header_spi_transfer(const uint8_t *tx, uint8_t *out_rx, size_t length) {
  if (!tx || !out_rx || !length || length > HEADER_SPI_TRANSFER_MAX)
    return ESP_ERR_INVALID_ARG;
  if (!s_device)
    return ESP_ERR_INVALID_STATE;
  if (!spi_bus_lock_take(HEADER_SPI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  memcpy(s_tx, tx, length);
  spi_transaction_t transaction = {
      .length = length * BITS_PER_BYTE,
      .tx_buffer = s_tx,
      .rx_buffer = s_rx,
  };
  /* Own the hardware bus before asserting software CS, including queued LCD DMA. */
  esp_err_t error = spi_device_acquire_bus(s_device, portMAX_DELAY);
  if (error != ESP_OK) {
    spi_bus_lock_give();
    return error;
  }
  error = gpio_set_level(pin_gpio(s_config.cs_pin), 0);
  if (error == ESP_OK)
    error = spi_device_polling_transmit(s_device, &transaction);
  esp_err_t cs_error = gpio_set_level(pin_gpio(s_config.cs_pin), 1);
  spi_device_release_bus(s_device);
  if (error == ESP_OK)
    error = cs_error;
  if (error == ESP_OK)
    memcpy(out_rx, s_rx, length);
  spi_bus_lock_give();
  return error;
}

esp_err_t header_spi_write(uint8_t pin, bool level) {
  if (!s_device || pin != s_config.control_pin)
    return ESP_ERR_INVALID_STATE;
  return gpio_set_level(pin_gpio(pin), level);
}

esp_err_t header_spi_close(void) {
  if (!s_device) {
    if (!s_has_sleep_hold)
      return ESP_OK;
    esp_err_t release_error = power_manager_no_sleep_release();
    if (release_error == ESP_OK)
      s_has_sleep_hold = false;
    return release_error;
  }
  esp_err_t error = gpio_set_level(pin_gpio(s_config.control_pin), 0);
  if (error != ESP_OK)
    return error;
  if (!spi_bus_lock_take(HEADER_SPI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  error = spi_bus_remove_device(s_device);
  if (error == ESP_OK) {
    s_device = NULL;
    esp_err_t control_error = configure_pin(pin_gpio(s_config.control_pin), false, false);
    esp_err_t cs_error = configure_pin(pin_gpio(s_config.cs_pin), false, true);
    if (control_error != ESP_OK || cs_error != ESP_OK)
      ESP_LOGE(TAG, "Header idle pin configuration failed");
    ESP_LOGI(TAG, "Header SPI released");
  }
  spi_bus_lock_give();
  if (error == ESP_OK && s_has_sleep_hold) {
    error = power_manager_no_sleep_release();
    if (error == ESP_OK)
      s_has_sleep_hold = false;
  }
  return error;
}

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

#include "sx1262_hal.h"

#include <string.h>

#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "pin_def.h"
#include "sx1262_regs.h"

#define PIN_SCK  GPIO_LORA_SCLK_PIN
#define PIN_MOSI GPIO_LORA_MOSI_PIN
#define PIN_MISO GPIO_LORA_MISO_PIN
#define PIN_NSS  GPIO_LORA_CS_PIN
#define PIN_NRST GPIO_LORA_RESET_PIN
#define PIN_BUSY GPIO_LORA_BUSY_PIN
#define PIN_DIO1 GPIO_LORA_DIO1_PIN
#define PIN_TXEN GPIO_LORA_TXEN_PIN
#define PIN_RXEN GPIO_LORA_RXEN_PIN

#define SPI_HOST_ID      SPI3_HOST
#define SPI_FREQ_HZ      8000000
#define SPI_MAX_TRANSFER 264

static const char *TAG = "SX1262_HAL_ESP32";

typedef struct {
  spi_device_handle_t spi;
  SemaphoreHandle_t spi_mutex;
  portMUX_TYPE critical_mux;
  bool is_initialized;
} hal_esp32_ctx_t;

static hal_esp32_ctx_t s_ctx = {
    .spi = NULL,
    .spi_mutex = NULL,
    .critical_mux = portMUX_INITIALIZER_UNLOCKED,
    .is_initialized = false,
};

static int hal_spi_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
  hal_esp32_ctx_t *c = (hal_esp32_ctx_t *)ctx;

  spi_transaction_t t = {
      .length = len * 8,
      .tx_buffer = tx,
      .rx_buffer = rx,
  };

  esp_err_t ret = spi_device_polling_transmit(c->spi, &t);
  return (ret == ESP_OK) ? 0 : -1;
}

static void hal_cs_low(void *ctx) {
  (void)ctx;
  gpio_set_level(PIN_NSS, 0);
}

static void hal_cs_high(void *ctx) {
  (void)ctx;
  gpio_set_level(PIN_NSS, 1);
}

static void hal_reset_write(void *ctx, uint8_t level) {
  (void)ctx;
  if (PIN_NRST < 0) {
    return;
  }
  gpio_set_level(PIN_NRST, level ? 1 : 0);
}

static uint8_t hal_busy_read(void *ctx) {
  (void)ctx;
  return (uint8_t)gpio_get_level(PIN_BUSY);
}

static void hal_delay_ms(void *ctx, uint32_t ms) {
  (void)ctx;
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static uint32_t hal_get_tick_ms(void *ctx) {
  (void)ctx;
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void hal_lock(void *ctx) {
  hal_esp32_ctx_t *c = (hal_esp32_ctx_t *)ctx;
  xSemaphoreTake(c->spi_mutex, portMAX_DELAY);
  spi_device_acquire_bus(c->spi, portMAX_DELAY);
}

static void hal_unlock(void *ctx) {
  hal_esp32_ctx_t *c = (hal_esp32_ctx_t *)ctx;
  spi_device_release_bus(c->spi);
  xSemaphoreGive(c->spi_mutex);
}

static void hal_enter_critical(void *ctx) {
  hal_esp32_ctx_t *c = (hal_esp32_ctx_t *)ctx;
  portENTER_CRITICAL(&c->critical_mux);
}

static void hal_exit_critical(void *ctx) {
  hal_esp32_ctx_t *c = (hal_esp32_ctx_t *)ctx;
  portEXIT_CRITICAL(&c->critical_mux);
}

static void hal_set_antenna(void *ctx, uint8_t mode) {
  (void)ctx;
  if (PIN_TXEN < 0 || PIN_RXEN < 0) {
    return;
  }
  switch (mode) {
    case SX1262_ANT_TX:
      gpio_set_level(PIN_TXEN, 1);
      gpio_set_level(PIN_RXEN, 0);
      break;
    case SX1262_ANT_RX:
      gpio_set_level(PIN_TXEN, 0);
      gpio_set_level(PIN_RXEN, 1);
      break;
    default: /* SX1262_ANT_OFF */
      gpio_set_level(PIN_TXEN, 0);
      gpio_set_level(PIN_RXEN, 0);
      break;
  }
}

esp_err_t sx1262_hal_create(sx1262_hal_t *out_hal) {
  if (out_hal == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_ctx.is_initialized) {
    ESP_LOGW(TAG, "HAL already initialized");
    goto fill_hal;
  }

  uint64_t out_mask = (1ULL << PIN_NSS);
  if (PIN_NRST >= 0) {
    out_mask |= (1ULL << PIN_NRST);
  }
  if (PIN_TXEN >= 0) {
    out_mask |= (1ULL << PIN_TXEN);
  }
  if (PIN_RXEN >= 0) {
    out_mask |= (1ULL << PIN_RXEN);
  }
  gpio_config_t out_conf = {
      .pin_bit_mask = out_mask,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t ret = gpio_config(&out_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GPIO output config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  gpio_set_level(PIN_NSS, 1);
  if (PIN_TXEN >= 0) {
    gpio_set_level(PIN_TXEN, 0);
  }
  if (PIN_RXEN >= 0) {
    gpio_set_level(PIN_RXEN, 0);
  }

  gpio_config_t in_conf = {
      .pin_bit_mask = (1ULL << PIN_BUSY) | (1ULL << PIN_DIO1),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ret = gpio_config(&in_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GPIO input config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = PIN_MOSI,
      .miso_io_num = PIN_MISO,
      .sclk_io_num = PIN_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = SPI_MAX_TRANSFER,
  };

  ret = spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO);
  if (ret == ESP_ERR_INVALID_STATE) {
    /* Bus already initialized by another driver (e.g. ST7789 via kernel spi_init). */
    ret = ESP_OK;
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  spi_device_interface_config_t dev_cfg = {
      .clock_speed_hz = SPI_FREQ_HZ,
      .mode = 0,
      .spics_io_num = -1,
      .queue_size = 1,
  };

  ret = spi_bus_add_device(SPI_HOST_ID, &dev_cfg, &s_ctx.spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI add device failed: %s", esp_err_to_name(ret));
    return ret;
  }

  /* ── Mutex ──────────────────────────────────────────────────── */
  s_ctx.spi_mutex = xSemaphoreCreateMutex();
  if (s_ctx.spi_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create SPI mutex");
    spi_bus_remove_device(s_ctx.spi);
    s_ctx.spi = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_ctx.is_initialized = true;
  ESP_LOGI(TAG,
           "Hardware initialized — SPI@%dMHz, NSS=GPIO%d, BUSY=GPIO%d",
           SPI_FREQ_HZ / 1000000,
           PIN_NSS,
           PIN_BUSY);

fill_hal:
  out_hal->spi_transfer = hal_spi_transfer;
  out_hal->cs_low = hal_cs_low;
  out_hal->cs_high = hal_cs_high;
  out_hal->reset_write = hal_reset_write;
  out_hal->busy_read = hal_busy_read;
  out_hal->delay_ms = hal_delay_ms;
  out_hal->get_tick_ms = hal_get_tick_ms;
  out_hal->lock = hal_lock;
  out_hal->unlock = hal_unlock;
  out_hal->enter_critical = hal_enter_critical;
  out_hal->exit_critical = hal_exit_critical;
  out_hal->set_antenna = hal_set_antenna;
  out_hal->ctx = &s_ctx;

  return ESP_OK;
}

esp_err_t sx1262_hal_destroy(void) {
  if (!s_ctx.is_initialized) {
    return ESP_OK;
  }

  if (s_ctx.spi != NULL) {
    spi_bus_remove_device(s_ctx.spi);
    s_ctx.spi = NULL;
  }
  if (s_ctx.spi_mutex != NULL) {
    vSemaphoreDelete(s_ctx.spi_mutex);
    s_ctx.spi_mutex = NULL;
  }

  /* SPI3 bus is shared with ST7789 display; do not free it here. */

  s_ctx.is_initialized = false;
  ESP_LOGI(TAG, "HAL destroyed (SPI3 bus left intact for shared peripherals)");
  return ESP_OK;
}

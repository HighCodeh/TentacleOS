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

#include "spi_slave_driver.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "hal/gpio_ll.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#include "pin_def.h"

static const char *TAG = "SPI_SLAVE_DRV";

// The atomic publication below is intentionally target-specific. In this C5
// IDF path, validated DMA buffers need neither heap allocation nor cache sync
// in spi_slave_queue_trans(). Revisit rather than silently porting to a target
// where the driver may allocate or block inside the critical section.
#if !CONFIG_IDF_TARGET_ESP32C5 || !CONFIG_FREERTOS_UNICORE || SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#error "SPI paired publication requires single-core C5 without internal-memory cache"
#endif

#define SPI_PAIR_MAX_BYTES 2080u
static portMUX_TYPE s_pair_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR portMUX_TYPE s_setup_diag_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR spi_slave_setup_diag_t s_setup_diag;

#define SPI_PENDING_COUNT    3
#define SPI_RECOVERY_POLL_MS 10
static spi_slave_transaction_t *s_pending[SPI_PENDING_COUNT];
static bool s_has_unknown_owner;

static bool has_pending(void) {
  for (size_t i = 0; i < SPI_PENDING_COUNT; ++i)
    if (s_pending[i])
      return true;
  return false;
}

static bool release_pending(spi_slave_transaction_t *trans) {
  for (size_t i = 0; i < SPI_PENDING_COUNT; ++i) {
    if (trans && s_pending[i] == trans) {
      s_pending[i] = NULL;
      return true;
    }
  }
  s_has_unknown_owner = true;
  return false;
}

static esp_err_t queue_tracked(spi_slave_transaction_t *trans) {
  if (s_has_unknown_owner)
    return ESP_ERR_INVALID_STATE;
  size_t slot = SPI_PENDING_COUNT;
  for (size_t i = 0; i < SPI_PENDING_COUNT; ++i) {
    if (s_pending[i] == trans)
      return ESP_ERR_INVALID_STATE;
    if (!s_pending[i])
      slot = i;
  }
  if (slot == SPI_PENDING_COUNT)
    return ESP_ERR_NO_MEM;
  esp_err_t ret = spi_slave_queue_trans(SPI2_HOST, trans, 0);
  if (ret == ESP_OK)
    s_pending[slot] = trans;
  return ret;
}

esp_err_t spi_slave_driver_recover(uint32_t timeout_ms) {
  if (s_has_unknown_owner)
    return ESP_ERR_INVALID_STATE;
  int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
  while (has_pending()) {
    if (esp_timer_get_time() >= deadline)
      return ESP_ERR_TIMEOUT;
    spi_slave_transaction_t *result = NULL;
    esp_err_t ret = spi_slave_get_trans_result(SPI2_HOST, &result, 0);
    if (ret == ESP_ERR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(SPI_RECOVERY_POLL_MS));
      continue;
    }
    if (ret != ESP_OK)
      return ret;
    if (!release_pending(result))
      return ESP_ERR_INVALID_RESPONSE;
  }
  return ESP_OK;
}

static void IRAM_ATTR post_setup_observe(spi_slave_transaction_t *trans) {
  if (!trans)
    return;
  // IDF invokes this AFTER spi_slave_hal_user_start(). An active CS here is
  // an overlap observation, not proof CS was low when arming began: the
  // master can assert CS in the interval before this register sample. Read
  // the register directly so the callback never depends on flash-resident
  // gpio_get_level(). No payload, allocation, logging or timing wait here.
  bool cs_low = gpio_ll_get_level(GPIO_LL_GET_HW(0), GPIO_BRIDGE_CS_PIN) == 0;
  portENTER_CRITICAL_ISR(&s_setup_diag_lock);
  if (trans->tx_buffer) {
    if (s_setup_diag.tx_armed != UINT32_MAX)
      s_setup_diag.tx_armed++;
    if (cs_low && s_setup_diag.tx_cs_low_after_setup != UINT32_MAX)
      s_setup_diag.tx_cs_low_after_setup++;
  }
  if (trans->rx_buffer) {
    if (s_setup_diag.rx_armed != UINT32_MAX)
      s_setup_diag.rx_armed++;
    if (cs_low && s_setup_diag.rx_cs_low_after_setup != UINT32_MAX)
      s_setup_diag.rx_cs_low_after_setup++;
  }
  portEXIT_CRITICAL_ISR(&s_setup_diag_lock);
}

esp_err_t spi_slave_driver_get_setup_diag(spi_slave_setup_diag_t *out) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  spi_slave_setup_diag_t snapshot;
  portENTER_CRITICAL(&s_setup_diag_lock);
  snapshot = s_setup_diag;
  portEXIT_CRITICAL(&s_setup_diag_lock);
  // A caller's destination need not be internal RAM. Keep its write outside
  // the interrupt-masked window; the captured values remain one snapshot.
  *out = snapshot;
  return ESP_OK;
}

static bool pair_buffer_valid(const void *data, size_t len) {
  return data && len && len <= SPI_PAIR_MAX_BYTES && !(len & 3u) && esp_ptr_word_aligned(data) &&
         esp_ptr_dma_capable(data);
}

esp_err_t spi_slave_driver_init(void) {
  spi_bus_config_t buscfg = {
      .miso_io_num = GPIO_BRIDGE_MISO_PIN,
      .mosi_io_num = GPIO_BRIDGE_MOSI_PIN,
      .sclk_io_num = GPIO_BRIDGE_SCLK_PIN,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };

  spi_slave_interface_config_t slvcfg = {
      .mode = 0,
      .spics_io_num = GPIO_BRIDGE_CS_PIN,
      .queue_size = 3,
      .flags = 0,
      .post_setup_cb = post_setup_observe,
  };

  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << GPIO_BRIDGE_IRQ_PIN),
      .pull_down_en = 1,
      .pull_up_en = 0,
  };
  gpio_config(&io_conf);
  gpio_set_level(GPIO_BRIDGE_IRQ_PIN, 0);

  esp_err_t ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI slave: %s", esp_err_to_name(ret));
    return ret;
  }

  // Successful initialization has no queued transaction yet. Do not clear
  // observations on a failed duplicate init of an already running driver.
  portENTER_CRITICAL(&s_setup_diag_lock);
  s_setup_diag = (spi_slave_setup_diag_t){0};
  portEXIT_CRITICAL(&s_setup_diag_lock);

  ESP_LOGI(TAG, "SPI slave driver initialized");
  return ESP_OK;
}

esp_err_t spi_slave_driver_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
  spi_slave_transaction_t t = {
      .length = len * 8,
      .tx_buffer = tx_data,
      .rx_buffer = rx_data,
  };
  return spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);
}

esp_err_t spi_slave_driver_queue(spi_slave_transaction_t *trans,
                                 const uint8_t *tx_data,
                                 uint8_t *rx_data,
                                 size_t len) {
  if (!trans || has_pending() || s_has_unknown_owner)
    return ESP_ERR_INVALID_STATE;
  memset(trans, 0, sizeof(*trans));
  trans->length = len * 8;
  trans->tx_buffer = tx_data;
  trans->rx_buffer = rx_data;
  return queue_tracked(trans);
}

esp_err_t spi_slave_driver_wait(void) {
  spi_slave_transaction_t *result = NULL;
  esp_err_t ret = spi_slave_get_trans_result(SPI2_HOST, &result, portMAX_DELAY);
  if (ret == ESP_OK && !release_pending(result))
    return ESP_ERR_INVALID_RESPONSE;
  return ret;
}

esp_err_t spi_slave_driver_queue_response_and_receive(spi_slave_transaction_t *tx_trans,
                                                      const uint8_t *tx_data,
                                                      size_t tx_len,
                                                      spi_slave_transaction_t *rx_trans,
                                                      uint8_t *rx_data,
                                                      size_t rx_len) {
  // Reject descriptor/length/alignment errors before entering the critical
  // section, avoiding the IDF driver's error-logging paths while interrupts
  // are masked. The queue is initialized once and has three slots; the sole
  // caller has already reaped both prior transactions before entering here.
  if (!tx_trans || !rx_trans || tx_trans == rx_trans || !pair_buffer_valid(tx_data, tx_len) ||
      !pair_buffer_valid(rx_data, rx_len))
    return ESP_ERR_INVALID_ARG;
  uintptr_t tx_addr = (uintptr_t)tx_data, rx_addr = (uintptr_t)rx_data;
  if ((tx_addr <= rx_addr && rx_addr - tx_addr < tx_len) ||
      (rx_addr < tx_addr && tx_addr - rx_addr < rx_len))
    return ESP_ERR_INVALID_ARG;

  if (has_pending() || s_has_unknown_owner)
    return ESP_ERR_INVALID_STATE;
  memset(tx_trans, 0, sizeof(*tx_trans));
  memset(rx_trans, 0, sizeof(*rx_trans));
  tx_trans->length = tx_len * 8;
  tx_trans->tx_buffer = tx_data;
  rx_trans->length = rx_len * 8;
  rx_trans->rx_buffer = rx_data;

  // C5 is single-core: neither task preemption nor the SPI setup ISR can
  // expose the response before the next receive has been queued. These are
  // strictly nonblocking calls; never use portMAX_DELAY within this section.
  portENTER_CRITICAL(&s_pair_lock);
  esp_err_t ret = queue_tracked(tx_trans);
  if (ret == ESP_OK)
    ret = queue_tracked(rx_trans);
  portEXIT_CRITICAL(&s_pair_lock);
  return ret;
}

esp_err_t spi_slave_driver_wait_for_ticks(spi_slave_transaction_t *expected, TickType_t ticks) {
  if (!expected)
    return ESP_ERR_INVALID_ARG;
  spi_slave_transaction_t *result = NULL;
  esp_err_t ret = spi_slave_get_trans_result(SPI2_HOST, &result, ticks);
  if (ret != ESP_OK)
    return ret;
  if (!release_pending(result) || result != expected)
    return ESP_ERR_INVALID_RESPONSE;
  if (result->trans_len != expected->length)
    return ESP_ERR_INVALID_SIZE;
  return ESP_OK;
}

void spi_slave_driver_set_irq(int level) {
  gpio_set_level(GPIO_BRIDGE_IRQ_PIN, level);
}

esp_err_t spi_slave_driver_wait_for(spi_slave_transaction_t *expected) {
  return spi_slave_driver_wait_for_ticks(expected, portMAX_DELAY);
}

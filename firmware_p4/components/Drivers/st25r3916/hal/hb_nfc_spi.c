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

#include "hb_nfc_spi.h"

#include <string.h>

#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_err.h"

#include "spi.h"
#include "rf_frontend.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"

static const char *TAG = "hb_nfc_spi";

/* ST25R3916 SPI framing constants (datasheet DocID 031020 Rev 3, Sec. 6). */
#define HB_NFC_SPI_READ_FLAG        0x40 /**< Set bit 6 to select a read operation. */
#define HB_NFC_SPI_ADDR_MASK        0x3F /**< Register address occupies bits [5:0]. */
#define HB_NFC_SPI_REG_XFER_BITS    16   /**< Bit count for a 2-byte register transaction. */
#define HB_NFC_SPI_CMD_XFER_BITS    8    /**< Bit count for a 1-byte direct command. */
#define HB_NFC_SPI_FIFO_LOAD_BYTE   0x80 /**< FIFO load prefix byte. */
#define HB_NFC_SPI_FIFO_READ_BYTE   0x9F /**< FIFO read prefix byte. */
#define HB_NFC_SPI_PT_MEM_READ_BYTE 0xBF /**< Passive target memory read prefix byte. */
#define HB_NFC_SPI_FIFO_MAX_BYTES   512  /**< ST25R3916 FIFO capacity. */
#define HB_NFC_SPI_PT_MEM_MAX_LEN   19   /**< Maximum PT memory payload (NFC-F section). */
#define HB_NFC_SPI_PT_MEM_HDR_BYTES 2    /**< Bytes before payload in PT memory read response. */
#define HB_NFC_SPI_RAW_XFER_MAX_LEN 64   /**< Maximum length for hb_nfc_spi_raw_xfer(). */

static spi_device_handle_t s_spi = NULL;
static bool s_is_init = false;
static int s_spi_host = -1;

static esp_err_t transmit(spi_transaction_t *t);

esp_err_t hb_nfc_spi_init(const hb_nfc_spi_config_t *config) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_is_init)
    return ESP_OK;

  esp_err_t ret = spi_bus_init(
      (spi_host_device_t)config->spi_host, config->pin_mosi, config->pin_miso, config->pin_sclk);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "bus init fail: %s", esp_err_to_name(ret));
    return ret;
  }

  spi_device_config_t dev = {
      .clock_speed_hz = config->clock_hz,
      .mode = config->mode,
      .cs_pin = config->pin_cs,
      .queue_size = 1,
  };
  ret = spi_add_device((spi_host_device_t)config->spi_host, SPI_DEVICE_NFC, &dev);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "add device fail: %s", esp_err_to_name(ret));
    return ret;
  }
  s_spi = spi_get_handle(SPI_DEVICE_NFC);
  if (s_spi == NULL) {
    return ESP_FAIL;
  }

  s_spi_host = config->spi_host;
  s_is_init = true;
  ESP_LOGI(TAG,
           "SPI OK: mode=%u clk=%lu cs=%d",
           config->mode,
           (unsigned long)config->clock_hz,
           config->pin_cs);
  return ESP_OK;
}

void hb_nfc_spi_deinit(void) {
  if (!s_is_init)
    return;
  if (s_spi != NULL) {
    spi_remove_device(SPI_DEVICE_NFC);
    s_spi = NULL;
  }
  s_spi_host = -1;
  s_is_init = false;
}

esp_err_t hb_nfc_spi_reg_read(uint8_t addr, uint8_t *out_value) {
  if (out_value == NULL)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[2] = {(uint8_t)(HB_NFC_SPI_READ_FLAG | (addr & HB_NFC_SPI_ADDR_MASK)), 0x00};
  uint8_t rx[2] = {0};
  spi_transaction_t t = {
      .length = HB_NFC_SPI_REG_XFER_BITS,
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK) {
    *out_value = 0;
    return err;
  }
  *out_value = rx[1];
  return ESP_OK;
}

esp_err_t hb_nfc_spi_reg_write(uint8_t addr, uint8_t value) {
  const bool controls_field = (addr & HB_NFC_SPI_ADDR_MASK) == ST25R3916_REG_OP_CTRL;
  const bool enables_tx = controls_field && (value & ST25R3916_OP_CTRL_TX_EN) != 0;
  if (enables_tx) {
    esp_err_t claim = rf_frontend_claim(RF_FRONTEND_NFC_13MHZ);
    if (claim != ESP_OK) {
      ESP_LOGW(TAG, "refusing NFC TX: shared antenna is busy");
      return claim;
    }
  }

  uint8_t tx[2] = {(uint8_t)(addr & HB_NFC_SPI_ADDR_MASK), value};
  spi_transaction_t t = {
      .length = HB_NFC_SPI_REG_XFER_BITS,
      .tx_buffer = tx,
  };
  esp_err_t err = transmit(&t);
  if (controls_field && !enables_tx && err == ESP_OK) {
    rf_frontend_release(RF_FRONTEND_NFC_13MHZ);
  }
  return err;
}

esp_err_t hb_nfc_spi_reg_read_b(uint8_t addr, uint8_t *out_value) {
  if (out_value == NULL)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[3] = {
      ST25R3916_SPI_SPACE_B,
      (uint8_t)(HB_NFC_SPI_READ_FLAG | (addr & HB_NFC_SPI_ADDR_MASK)),
      0x00,
  };
  uint8_t rx[3] = {0};
  spi_transaction_t t = {
      .length = 24,
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK) {
    *out_value = 0;
    return err;
  }
  *out_value = rx[2];
  return ESP_OK;
}

esp_err_t hb_nfc_spi_reg_write_b(uint8_t addr, uint8_t value) {
  uint8_t tx[3] = {
      ST25R3916_SPI_SPACE_B,
      (uint8_t)(addr & HB_NFC_SPI_ADDR_MASK),
      value,
  };
  spi_transaction_t t = {
      .length = 24,
      .tx_buffer = tx,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_reg_modify(uint8_t addr, uint8_t mask, uint8_t value) {
  uint8_t cur;
  esp_err_t err = hb_nfc_spi_reg_read(addr, &cur);
  if (err != ESP_OK)
    return err;
  uint8_t nv = (cur & (uint8_t)~mask) | (value & mask);
  return hb_nfc_spi_reg_write(addr, nv);
}

esp_err_t hb_nfc_spi_fifo_load(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > HB_NFC_SPI_FIFO_MAX_BYTES)
    return ESP_ERR_INVALID_ARG;

  static uint8_t tx[1 + HB_NFC_SPI_FIFO_MAX_BYTES];
  tx[0] = HB_NFC_SPI_FIFO_LOAD_BYTE;
  memcpy(&tx[1], data, len);

  spi_transaction_t t = {
      .length = (uint32_t)((len + 1) * 8),
      .tx_buffer = tx,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_fifo_read(uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > HB_NFC_SPI_FIFO_MAX_BYTES)
    return ESP_ERR_INVALID_ARG;

  static uint8_t tx[1 + HB_NFC_SPI_FIFO_MAX_BYTES];
  static uint8_t rx[1 + HB_NFC_SPI_FIFO_MAX_BYTES];
  memset(tx, 0, len + 1);
  tx[0] = HB_NFC_SPI_FIFO_READ_BYTE;

  spi_transaction_t t = {
      .length = (uint32_t)((len + 1) * 8),
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK)
    return err;
  memcpy(data, &rx[1], len);
  return ESP_OK;
}

esp_err_t hb_nfc_spi_direct_cmd(uint8_t cmd) {
  spi_transaction_t t = {
      .length = HB_NFC_SPI_CMD_XFER_BITS,
      .tx_buffer = &cmd,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_pt_mem_write(uint8_t prefix, const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > HB_NFC_SPI_PT_MEM_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[1 + HB_NFC_SPI_PT_MEM_MAX_LEN];
  tx[0] = prefix;
  memcpy(&tx[1], data, len);

  spi_transaction_t t = {
      .length = (uint32_t)((len + 1) * 8),
      .tx_buffer = tx,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_pt_mem_read(uint8_t *out_data, size_t len) {
  if (out_data == NULL || len == 0 || len > HB_NFC_SPI_PT_MEM_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[HB_NFC_SPI_PT_MEM_HDR_BYTES + HB_NFC_SPI_PT_MEM_MAX_LEN];
  uint8_t rx[HB_NFC_SPI_PT_MEM_HDR_BYTES + HB_NFC_SPI_PT_MEM_MAX_LEN];
  memset(tx, 0, sizeof(tx));
  tx[0] = HB_NFC_SPI_PT_MEM_READ_BYTE;

  spi_transaction_t t = {
      .length = (uint32_t)((len + HB_NFC_SPI_PT_MEM_HDR_BYTES) * 8),
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK)
    return err;
  memcpy(out_data, &rx[HB_NFC_SPI_PT_MEM_HDR_BYTES], len);
  return ESP_OK;
}

esp_err_t hb_nfc_spi_raw_xfer(const uint8_t *tx, uint8_t *rx, size_t len) {
  if (tx == NULL || len == 0 || len > HB_NFC_SPI_RAW_XFER_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  spi_transaction_t t = {
      .length = (uint32_t)(len * 8),
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  return transmit(&t);
}

static esp_err_t transmit(spi_transaction_t *t) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  if (s_spi == NULL || t == NULL)
    return ESP_FAIL;

  const bool shared_lock = s_spi_host == SPI3_HOST;
  bool locked = !shared_lock || spi_bus_lock_take(100);
  if (!locked) {
    ESP_LOGW(TAG, "shared SPI bus lock timeout");
    return ESP_ERR_TIMEOUT;
  }

  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ)) {
    if (shared_lock)
      spi_bus_lock_give();
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = spi_device_transmit(s_spi, t);
  if (shared_lock) {
    spi_bus_lock_give();
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "spi transmit fail: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  return ESP_OK;
}

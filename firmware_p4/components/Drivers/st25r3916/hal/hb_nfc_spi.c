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

static const char *TAG = "hb_nfc_spi";

#define HB_NFC_BUS_LOCK_TIMEOUT_MS 100

#define HB_NFC_SPI_READ_FLAG        0x40
#define HB_NFC_SPI_ADDR_MASK        0x3F
#define HB_NFC_SPI_REG_XFER_BITS    16
#define HB_NFC_SPI_CMD_XFER_BITS    8
#define HB_NFC_SPI_FIFO_LOAD_BYTE   0x80
#define HB_NFC_SPI_FIFO_READ_BYTE   0x9F
#define HB_NFC_SPI_PT_MEM_READ_BYTE 0xBF
#define HB_NFC_SPI_FIFO_MAX_BYTES   512
#define HB_NFC_SPI_PT_MEM_MAX_LEN   19
#define HB_NFC_SPI_PT_MEM_HDR_BYTES 2
#define HB_NFC_SPI_RAW_XFER_MAX_LEN 64

static spi_device_handle_t s_spi = NULL;
static bool s_is_init = false;

static esp_err_t transmit(spi_transaction_t *t);

esp_err_t hb_nfc_spi_init(const hb_nfc_spi_config_t *config) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_is_init)
    return ESP_OK;

  esp_err_t ret = spi_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "shared bus init fail: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  spi_device_interface_config_t dev = {
      .clock_speed_hz = config->clock_hz,
      .mode = config->mode,
      .spics_io_num = config->pin_cs,
      .queue_size = 1,
      .cs_ena_pretrans = 1,
      .cs_ena_posttrans = 1,
  };
  ret = spi_bus_add_device(config->spi_host, &dev, &s_spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "add device fail: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  s_is_init = true;
  ESP_LOGI(TAG,
           "SPI OK (shared bus, host %d): mode=%u clk=%lu cs=%d",
           config->spi_host,
           config->mode,
           (unsigned long)config->clock_hz,
           config->pin_cs);
  return ESP_OK;
}

void hb_nfc_spi_deinit(void) {
  if (!s_is_init)
    return;
  if (s_spi != NULL) {
    spi_bus_remove_device(s_spi);
    s_spi = NULL;
  }
  s_is_init = false;
}

esp_err_t hb_nfc_spi_reg_read(uint8_t addr, uint8_t *out_value) {
  if (out_value == NULL)
    return ESP_ERR_INVALID_ARG;

  spi_transaction_t t = {
      .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
      .length = HB_NFC_SPI_REG_XFER_BITS,
  };
  t.tx_data[0] = (uint8_t)(HB_NFC_SPI_READ_FLAG | (addr & HB_NFC_SPI_ADDR_MASK));
  t.tx_data[1] = 0x00;
  esp_err_t err = transmit(&t);
  if (err != ESP_OK) {
    *out_value = 0;
    return err;
  }
  *out_value = t.rx_data[1];
  return ESP_OK;
}

esp_err_t hb_nfc_spi_reg_write(uint8_t addr, uint8_t value) {
  spi_transaction_t t = {
      .flags = SPI_TRANS_USE_TXDATA,
      .length = HB_NFC_SPI_REG_XFER_BITS,
  };
  t.tx_data[0] = (uint8_t)(addr & HB_NFC_SPI_ADDR_MASK);
  t.tx_data[1] = value;
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
      .flags = SPI_TRANS_USE_TXDATA,
      .length = HB_NFC_SPI_CMD_XFER_BITS,
  };
  t.tx_data[0] = cmd;
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
  if (s_spi == NULL || t == NULL)
    return ESP_FAIL;

  bool locked = spi_bus_lock_take(HB_NFC_BUS_LOCK_TIMEOUT_MS);
  esp_err_t ret = spi_device_transmit(s_spi, t);
  if (locked)
    spi_bus_lock_give();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "spi transmit fail: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  return ESP_OK;
}

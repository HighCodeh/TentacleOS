// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sx1262_cmd.h"

#include <string.h>

#include "esp_log.h"

#include "sx1262_regs.h"

#define MAX_CMD_BUF_LEN 264

static const char *TAG = "SX1262_CMD";

static esp_err_t transfer_locked(sx1262_hal_t *hal, const uint8_t *tx, uint8_t *rx, size_t len);

esp_err_t sx1262_cmd_wait_busy(sx1262_hal_t *hal, uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    timeout_ms = SX1262_WAIT_BUSY_TIMEOUT_MS;
  }

  uint32_t start = hal->get_tick_ms(hal->ctx);

  while (hal->busy_read(hal->ctx)) {
    uint32_t elapsed = hal->get_tick_ms(hal->ctx) - start;
    if (elapsed >= timeout_ms) {
      ESP_LOGE(TAG, "BUSY timeout after %lu ms", (unsigned long)elapsed);
      return ESP_ERR_TIMEOUT;
    }
    hal->delay_ms(hal->ctx, 1);
  }

  return ESP_OK;
}

esp_err_t sx1262_cmd_write(sx1262_hal_t *hal, uint8_t opcode, const uint8_t *params, size_t len) {
  if (len > 0 && params == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t total = 1 + len;
  uint8_t tx_buf[MAX_CMD_BUF_LEN];

  tx_buf[0] = opcode;
  if (len > 0) {
    memcpy(&tx_buf[1], params, len);
  }

  esp_err_t ret = sx1262_cmd_wait_busy(hal, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  return transfer_locked(hal, tx_buf, NULL, total);
}

esp_err_t sx1262_cmd_read(sx1262_hal_t *hal, uint8_t opcode, uint8_t *out_result, size_t len) {
  if (len > 0 && out_result == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t total = 1 + 1 + len;
  uint8_t tx_buf[MAX_CMD_BUF_LEN];
  uint8_t rx_buf[MAX_CMD_BUF_LEN];

  memset(tx_buf, 0x00, total);
  memset(rx_buf, 0x00, total);
  tx_buf[0] = opcode;

  esp_err_t ret = sx1262_cmd_wait_busy(hal, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = transfer_locked(hal, tx_buf, rx_buf, total);
  if (ret != ESP_OK) {
    return ret;
  }

  if (len > 0) {
    memcpy(out_result, &rx_buf[2], len);
  }

  return ESP_OK;
}

esp_err_t
sx1262_cmd_write_register(sx1262_hal_t *hal, uint16_t addr, const uint8_t *data, size_t len) {
  if (len == 0 || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t total = 3 + len;
  uint8_t tx_buf[MAX_CMD_BUF_LEN];

  tx_buf[0] = SX1262_OP_WRITE_REGISTER;
  tx_buf[1] = (uint8_t)(addr >> 8);
  tx_buf[2] = (uint8_t)(addr & 0xFF);
  memcpy(&tx_buf[3], data, len);

  esp_err_t ret = sx1262_cmd_wait_busy(hal, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  return transfer_locked(hal, tx_buf, NULL, total);
}

esp_err_t
sx1262_cmd_read_register(sx1262_hal_t *hal, uint16_t addr, uint8_t *out_data, size_t len) {
  if (len == 0 || out_data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t total = 4 + len;
  uint8_t tx_buf[MAX_CMD_BUF_LEN];
  uint8_t rx_buf[MAX_CMD_BUF_LEN];

  memset(tx_buf, 0x00, total);
  memset(rx_buf, 0x00, total);
  tx_buf[0] = SX1262_OP_READ_REGISTER;
  tx_buf[1] = (uint8_t)(addr >> 8);
  tx_buf[2] = (uint8_t)(addr & 0xFF);

  esp_err_t ret = sx1262_cmd_wait_busy(hal, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = transfer_locked(hal, tx_buf, rx_buf, total);
  if (ret != ESP_OK) {
    return ret;
  }

  memcpy(out_data, &rx_buf[4], len);
  return ESP_OK;
}

esp_err_t
sx1262_cmd_write_buffer(sx1262_hal_t *hal, uint8_t offset, const uint8_t *data, size_t len) {
  if (len == 0 || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t total = 2 + len;
  uint8_t tx_buf[MAX_CMD_BUF_LEN];

  tx_buf[0] = SX1262_OP_WRITE_BUFFER;
  tx_buf[1] = offset;
  memcpy(&tx_buf[2], data, len);

  esp_err_t ret = sx1262_cmd_wait_busy(hal, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  return transfer_locked(hal, tx_buf, NULL, total);
}

esp_err_t sx1262_cmd_read_buffer(sx1262_hal_t *hal, uint8_t offset, uint8_t *out_data, size_t len) {
  if (len == 0 || out_data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t total = 3 + len;
  uint8_t tx_buf[MAX_CMD_BUF_LEN];
  uint8_t rx_buf[MAX_CMD_BUF_LEN];

  memset(tx_buf, 0x00, total);
  memset(rx_buf, 0x00, total);
  tx_buf[0] = SX1262_OP_READ_BUFFER;
  tx_buf[1] = offset;

  esp_err_t ret = sx1262_cmd_wait_busy(hal, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = transfer_locked(hal, tx_buf, rx_buf, total);
  if (ret != ESP_OK) {
    return ret;
  }

  memcpy(out_data, &rx_buf[3], len);
  return ESP_OK;
}

static esp_err_t transfer_locked(sx1262_hal_t *hal, const uint8_t *tx, uint8_t *rx, size_t len) {
  hal->lock(hal->ctx);
  hal->cs_low(hal->ctx);

  int spi_ret = hal->spi_transfer(hal->ctx, tx, rx, len);

  hal->cs_high(hal->ctx);
  hal->unlock(hal->ctx);

  if (spi_ret < 0) {
    ESP_LOGE(TAG, "SPI transfer failed: %d", spi_ret);
    return ESP_FAIL;
  }

  return ESP_OK;
}

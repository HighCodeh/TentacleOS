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

#include "st25r3916_fifo.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"

#define FIFO_US_PER_MS 1000

static const char *TAG = "ST25R3916_FIFO";

uint16_t st25r3916_fifo_get_count(void) {
  uint8_t lsb = 0;
  uint8_t msb = 0;

  hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS1, &lsb);
  hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS2, &msb);

  return (uint16_t)(((msb & ST25R3916_FIFO_STATUS2_MSB_MASK) << 2) | lsb);
}

void st25r3916_fifo_clear(void) {
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_CLEAR_FIFO);
}

esp_err_t st25r3916_fifo_load(const uint8_t *data, size_t len) {
  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return hb_nfc_spi_fifo_load(data, len);
}

esp_err_t st25r3916_fifo_read(uint8_t *out_data, size_t len) {
  if (out_data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return hb_nfc_spi_fifo_read(out_data, len);
}

void st25r3916_fifo_set_tx_bytes(uint16_t nbytes, uint8_t nbtx_bits) {
  uint8_t reg1 = (uint8_t)((nbytes >> 5) & 0xFF);
  uint8_t reg2 = (uint8_t)(((nbytes & ST25R3916_FIFO_TX_BYTES_LSB_MASK) << 3) |
                           (nbtx_bits & ST25R3916_FIFO_TX_BITS_MASK));

  hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES1, reg1);
  hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES2, reg2);
}

esp_err_t st25r3916_fifo_wait(size_t min_bytes, int32_t timeout_ms, uint16_t *out_final_count) {
  uint16_t current_count = 0;
  esp_err_t ret = ESP_ERR_TIMEOUT;

  const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * FIFO_US_PER_MS;

  while (esp_timer_get_time() < deadline) {
    current_count = st25r3916_fifo_get_count();
    if (current_count >= min_bytes) {
      ret = ESP_OK;
      break;
    }
    hb_nfc_timer_delay_us(ST25R3916_FIFO_WAIT_POLL_US);
  }

  if (out_final_count != NULL) {
    *out_final_count = current_count;
  }

  if (ret != ESP_OK) {
    ESP_LOGD(TAG,
             "FIFO wait timeout: expected %u, got %u",
             (unsigned)min_bytes,
             (unsigned)current_count);
  }

  return ret;
}

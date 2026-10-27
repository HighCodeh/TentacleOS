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

#include "st25r3916_irq.h"

#include <string.h>

#include "esp_log.h"

#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_gpio.h"
#include "hb_nfc_timer.h"

static const char *TAG = "ST25R3916_IRQ";

esp_err_t st25r3916_irq_read(st25r3916_irq_status_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &out_status->error);
  hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &out_status->timer);
  hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &out_status->main);
  hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &out_status->target);
  hb_nfc_spi_reg_read(ST25R3916_REG_COLLISION, &out_status->collision);

  return ESP_OK;
}

esp_err_t st25r3916_irq_clear(void) {
  uint8_t ignored = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &ignored) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &ignored) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &ignored) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &ignored) != ESP_OK) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

void st25r3916_irq_log(const char *ctx, uint16_t fifo_count) {
  if (ctx == NULL) {
    return;
  }

  st25r3916_irq_status_t status = {0};
  if (st25r3916_irq_read(&status) == ESP_OK) {
    ESP_LOGW(TAG,
             "%s IRQ: MAIN=0x%02X ERR=0x%02X TMR=0x%02X TGT=0x%02X COL=0x%02X FIFO=%u",
             ctx,
             status.main,
             status.error,
             status.timer,
             status.target,
             status.collision,
             fifo_count);
  }
}

esp_err_t st25r3916_irq_wait_txe(void) {
  uint8_t seen_main = 0;
  uint8_t seen_error = 0;
  for (int i = 0; i < ST25R3916_IRQ_TX_POLL_MAX; i++) {
    uint8_t main = 0;
    uint8_t error = 0;
    if (hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main) != ESP_OK ||
        hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &error) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to read IRQ status");
      return ESP_FAIL;
    }
    seen_main |= main;
    seen_error |= error;

    if (main & ST25R3916_IRQ_MAIN_TXE) {
      return ESP_OK;
    }

    /* ERROR_INT reports receive CRC/parity/framing and wake-up events, not a
     * transmitter failure. A delayed receive indication may arrive while we
     * are waiting for TXE, so retain it for timeout diagnostics but do not
     * abort a valid transmission. */
    hb_nfc_timer_delay_us(ST25R3916_IRQ_TX_POLL_US);
  }

  uint8_t op_ctrl = 0;
  uint8_t mode = 0;
  uint8_t aux = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &op_ctrl);
  hb_nfc_spi_reg_read(ST25R3916_REG_MODE, &mode);
  hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
  ESP_LOGW(TAG,
           "TX timeout: OP=0x%02X MODE=0x%02X AUX=0x%02X IRQ=0x%02X ERR=0x%02X pin=%d",
           op_ctrl,
           mode,
           aux,
           seen_main,
           seen_error,
           hb_nfc_gpio_irq_level());
  return ESP_ERR_TIMEOUT;
}

esp_err_t st25r3916_irq_wait_rxe(int32_t timeout_ms, st25r3916_irq_status_t *out_status) {
  st25r3916_irq_status_t seen = {0};
  int32_t iterations = (timeout_ms * 1000) / ST25R3916_FIFO_WAIT_POLL_US;
  if (iterations == 0 && timeout_ms > 0) {
    iterations = 1;
  }

  for (int32_t i = 0; i < iterations; i++) {
    st25r3916_irq_status_t current = {0};
    if (st25r3916_irq_read(&current) != ESP_OK) {
      if (out_status != NULL) {
        *out_status = seen;
      }
      return ESP_FAIL;
    }

    seen.main |= current.main;
    seen.timer |= current.timer;
    seen.error |= current.error;
    seen.target |= current.target;
    seen.collision |= current.collision;

    if ((seen.main & ST25R3916_IRQ_MAIN_RXE) != 0U) {
      if (out_status != NULL) {
        *out_status = seen;
      }
      return ESP_OK;
    }
    hb_nfc_timer_delay_us(ST25R3916_FIFO_WAIT_POLL_US);
  }

  if (out_status != NULL) {
    *out_status = seen;
  }
  return ESP_ERR_TIMEOUT;
}

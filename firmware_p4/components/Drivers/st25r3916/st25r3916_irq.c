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
#include "esp_timer.h"

#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_gpio.h"
#include "hb_nfc_timer.h"

#define IRQ_US_PER_MS 1000

static uint8_t s_pending_main;
static uint8_t s_pending_error;

static const char *TAG = "ST25R3916_IRQ";

esp_err_t st25r3916_irq_read(st25r3916_irq_status_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  st25r3916_irq_status_t status = {0};
  memset(out_status, 0, sizeof(*out_status));
  if (hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &status.error) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &status.timer) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &status.main) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &status.target) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_COLLISION, &status.collision) != ESP_OK)
    return ESP_FAIL;
  status.main |= s_pending_main;
  status.error |= s_pending_error;
  s_pending_main = 0;
  s_pending_error = 0;
  *out_status = status;

  return ESP_OK;
}

esp_err_t st25r3916_irq_clear(void) {
  s_pending_main = 0;
  s_pending_error = 0;
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
  const int64_t deadline =
      esp_timer_get_time() + (int64_t)ST25R3916_IRQ_TX_POLL_MAX * ST25R3916_IRQ_TX_POLL_US;
  while (esp_timer_get_time() < deadline) {
    uint8_t main = 0;
    uint8_t error = 0;
    if (hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main) != ESP_OK ||
        hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &error) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to read IRQ status");
      return ESP_FAIL;
    }
    seen_main |= main;
    seen_error |= error;
    /* RX can complete in the same read-to-clear snapshot as TXE. */
    s_pending_main |= main & (uint8_t)~ST25R3916_IRQ_MAIN_TXE;
    s_pending_error |= error;

    if (main & ST25R3916_IRQ_MAIN_TXE) {
      return ESP_OK;
    }

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
  const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * IRQ_US_PER_MS;

  while (esp_timer_get_time() < deadline) {
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

esp_err_t st25r3916_irq_wait_exchange(int32_t timeout_ms, st25r3916_irq_status_t *out_status) {
  if (out_status == NULL || timeout_ms <= 0 || timeout_ms > 5000)
    return ESP_ERR_INVALID_ARG;
  memset(out_status, 0, sizeof(*out_status));
  int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
  const uint8_t complete = ST25R3916_IRQ_MAIN_TXE | ST25R3916_IRQ_MAIN_RXE;
  while (esp_timer_get_time() < deadline) {
    st25r3916_irq_status_t current = {0};
    esp_err_t err = st25r3916_irq_read(&current);
    if (err != ESP_OK)
      return err;
    out_status->main |= current.main;
    out_status->error |= current.error;
    out_status->timer |= current.timer;
    out_status->target |= current.target;
    out_status->collision |= current.collision;
    if ((out_status->main & complete) == complete) {
      /* ERROR_INT is read before MAIN_INT; collect errors that arrived at RXE. */
      uint8_t late = 0;
      err = hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &late);
      out_status->error |= late;
      return err;
    }
    hb_nfc_timer_delay_us(ST25R3916_FIFO_WAIT_POLL_US);
  }
  return ESP_ERR_TIMEOUT;
}

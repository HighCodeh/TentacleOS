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
/**
 * @file nfc_poller.c
 * @brief NFC poller transceive engine.
 */
#include "nfc_poller.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "nfc_common.h"
#include "st25r3916_core.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"
#include "nfc_rf.h"

static const char *TAG = "nfc_poller";

#define NFC_POLLER_FIFO_CAPACITY       512U
#define NFC_POLLER_PARTIAL_BYTE_MASK   0x0EU
#define NFC_POLLER_CRC_ERROR           0x80U
#define NFC_POLLER_CLASSIC_NONCE_BYTES 4U
#define NFC_POLLER_CLASSIC_AUTH_BYTES  2U
#define NFC_POLLER_CLASSIC_KEY_A       0x60U
#define NFC_POLLER_CLASSIC_KEY_B       0x61U
#define NFC_POLLER_FIFO_ERROR_MASK     0x30U

static uint32_t s_guard_time_us = 0;
static uint32_t s_fdt_min_us = 0;
static bool s_validate_fdt = false;
static uint32_t s_last_fdt_us = 0;

hb_nfc_err_t nfc_poller_start(void) {
  nfc_rf_config_t cfg = {
      .tech = NFC_RF_TECH_A,
      .tx_rate = NFC_RF_BR_106,
      .rx_rate = NFC_RF_BR_106,
      .am_mod_percent = 0,
      .tx_parity = true,
      .rx_raw_parity = false,
      .guard_time_us = 0,
      .fdt_min_us = 0,
      .validate_fdt = false,
  };
  hb_nfc_err_t err = nfc_rf_apply(&cfg);
  if (err != HB_NFC_OK)
    return err;
  return st25r3916_core_field_on();
}

void nfc_poller_stop(void) {
  st25r3916_core_field_off();
}

typedef struct {
  const uint8_t *tx;
  size_t tx_len;
  bool with_crc;
  uint8_t *rx;
  size_t rx_max;
  size_t rx_min;
  int timeout_ms;
  bool is_classic_nonce;
  bool is_passive_ack;
} nfc_poller_exchange_t;

static int exchange(const nfc_poller_exchange_t *request) {
  const uint8_t *tx = request->tx;
  size_t tx_len = request->tx_len;
  bool with_crc = request->with_crc;
  uint8_t *rx = request->rx;
  size_t rx_max = request->rx_max;
  size_t rx_min = request->rx_min;
  int timeout_ms = request->timeout_ms;
  if (tx == NULL || rx == NULL || tx_len == 0 || tx_len > NFC_POLLER_FIFO_CAPACITY || rx_max == 0 ||
      rx_min > rx_max || timeout_ms <= 0)
    return 0;
  if (s_guard_time_us > 0) {
    hb_nfc_timer_delay_us(s_guard_time_us);
  }

  int64_t t0 = esp_timer_get_time();

  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL) != ESP_OK ||
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_RESET_RX_GAIN) != ESP_OK ||
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_CLEAR_FIFO) != ESP_OK)
    return 0;

  if (st25r3916_irq_clear() != ESP_OK) {
    ESP_LOGW(TAG, "Could not clear stale IRQ status");
    return 0;
  }

  if (hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES1, (uint8_t)(tx_len >> 5)) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES2,
                           (uint8_t)((tx_len & ST25R3916_FIFO_TX_BYTES_LSB_MASK) << 3)) != ESP_OK ||
      st25r3916_fifo_load(tx, tx_len) != ESP_OK ||
      hb_nfc_spi_direct_cmd(with_crc ? ST25R3916_CMD_TX_WITH_CRC : ST25R3916_CMD_TX_WO_CRC) !=
          ESP_OK)
    return 0;

  if (st25r3916_irq_wait_txe() != ESP_OK) {
    ESP_LOGW(TAG, "TX timeout");
    return 0;
  }

  st25r3916_irq_status_t rx_irq = {0};
  esp_err_t rx_wait = st25r3916_irq_wait_rxe(timeout_ms, &rx_irq);
  uint8_t fifo_low = 0, fifo_high = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS1, &fifo_low) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS2, &fifo_high) != ESP_OK)
    return 0;
  uint16_t count = (uint16_t)(((fifo_high & ST25R3916_FIFO_STATUS2_MSB_MASK) << 2) | fifo_low);

  int64_t t1 = esp_timer_get_time();
  if (t1 > t0) {
    s_last_fdt_us = (uint32_t)(t1 - t0);
    if (s_validate_fdt && s_fdt_min_us > 0 && s_last_fdt_us < s_fdt_min_us) {
      ESP_LOGW(TAG, "FDT below min: %uus < %uus", (unsigned)s_last_fdt_us, (unsigned)s_fdt_min_us);
    }
  }

  uint8_t error = rx_irq.error;
  if (request->is_classic_nonce && count == NFC_POLLER_CLASSIC_NONCE_BYTES)
    error &= (uint8_t)~NFC_POLLER_CRC_ERROR;
  bool rx_error = (request->is_classic_nonce && (fifo_high & NFC_POLLER_PARTIAL_BYTE_MASK) != 0U) ||
                  (fifo_high & NFC_POLLER_FIFO_ERROR_MASK) != 0U || error != 0U ||
                  (rx_irq.main & ST25R3916_IRQ_MAIN_COL) != 0U;
  if (request->is_passive_ack)
    return rx_wait == ESP_ERR_TIMEOUT && count == 0 && !rx_error &&
           !(rx_irq.main & ST25R3916_IRQ_MAIN_RXE) &&
           (fifo_high & NFC_POLLER_PARTIAL_BYTE_MASK) == 0;
  if (rx_wait != ESP_OK || count < rx_min || count > rx_max || rx_error) {
    if (count > 0) {
      size_t to_read = (count > rx_max) ? rx_max : count;
      st25r3916_fifo_read(rx, to_read);
      nfc_log_hex(" RX partial:", rx, to_read);
    }
    ESP_LOGW(TAG,
             "RX fail: wait=%s MAIN=0x%02X ERR=0x%02X TMR=0x%02X TGT=0x%02X COL=0x%02X FIFO=%u",
             esp_err_to_name(rx_wait),
             rx_irq.main,
             rx_irq.error,
             rx_irq.timer,
             rx_irq.target,
             rx_irq.collision,
             count);
    return 0;
  }

  if (st25r3916_fifo_read(rx, count) != ESP_OK)
    return 0;
  return (int)count;
}

int nfc_poller_transceive(const uint8_t *tx,
                          size_t tx_len,
                          bool with_crc,
                          uint8_t *rx,
                          size_t rx_max,
                          size_t rx_min,
                          int timeout_ms) {
  const nfc_poller_exchange_t request = {.tx = tx,
                                         .tx_len = tx_len,
                                         .with_crc = with_crc,
                                         .rx = rx,
                                         .rx_max = rx_max,
                                         .rx_min = rx_min,
                                         .timeout_ms = timeout_ms};
  return exchange(&request);
}

bool nfc_poller_expect_silence(const uint8_t *tx, size_t tx_len, int timeout_ms) {
  uint8_t response = 0;
  const nfc_poller_exchange_t request = {.tx = tx,
                                         .tx_len = tx_len,
                                         .with_crc = true,
                                         .rx = &response,
                                         .rx_max = sizeof(response),
                                         .rx_min = 1,
                                         .timeout_ms = timeout_ms,
                                         .is_passive_ack = true};
  return exchange(&request) == 1;
}

int nfc_poller_classic_nonce(const uint8_t auth[2], uint8_t nonce[4], int timeout_ms) {
  if (auth == NULL || (auth[0] != NFC_POLLER_CLASSIC_KEY_A && auth[0] != NFC_POLLER_CLASSIC_KEY_B))
    return 0;
  const nfc_poller_exchange_t request = {.tx = auth,
                                         .tx_len = NFC_POLLER_CLASSIC_AUTH_BYTES,
                                         .with_crc = true,
                                         .rx = nonce,
                                         .rx_max = NFC_POLLER_CLASSIC_NONCE_BYTES,
                                         .rx_min = NFC_POLLER_CLASSIC_NONCE_BYTES,
                                         .timeout_ms = timeout_ms,
                                         .is_classic_nonce = true};
  return exchange(&request);
}

void nfc_poller_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt) {
  s_guard_time_us = guard_time_us;
  s_fdt_min_us = fdt_min_us;
  s_validate_fdt = validate_fdt;
}

uint32_t nfc_poller_get_last_fdt_us(void) {
  return s_last_fdt_us;
}

void nfc_log_hex(const char *label, const uint8_t *data, size_t len) {
  char buf[128];
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 3 < sizeof(buf); i++) {
    pos +=
        (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%02X%s", data[i], (i + 1 < len) ? " " : "");
  }
  ESP_LOGI("nfc", "%s %s", label, buf);
}

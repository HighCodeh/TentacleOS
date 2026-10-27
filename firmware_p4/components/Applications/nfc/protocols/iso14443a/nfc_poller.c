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

int nfc_poller_transceive(const uint8_t *tx,
                          size_t tx_len,
                          bool with_crc,
                          uint8_t *rx,
                          size_t rx_max,
                          size_t rx_min,
                          int timeout_ms) {
  if (s_guard_time_us > 0) {
    hb_nfc_timer_delay_us(s_guard_time_us);
  }

  int64_t t0 = esp_timer_get_time();

  hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_RESET_RX_GAIN);
  st25r3916_fifo_clear();

  if (st25r3916_irq_clear() != ESP_OK) {
    ESP_LOGW(TAG, "Could not clear stale IRQ status");
    return 0;
  }

  st25r3916_fifo_set_tx_bytes((uint16_t)tx_len, 0);

  st25r3916_fifo_load(tx, tx_len);

  hb_nfc_spi_direct_cmd(with_crc ? ST25R3916_CMD_TX_WITH_CRC : ST25R3916_CMD_TX_WO_CRC);

  if (st25r3916_irq_wait_txe() != ESP_OK) {
    ESP_LOGW(TAG, "TX timeout");
    return 0;
  }

  st25r3916_irq_status_t rx_irq = {0};
  esp_err_t rx_wait = st25r3916_irq_wait_rxe(timeout_ms, &rx_irq);
  uint16_t count = st25r3916_fifo_get_count();

  int64_t t1 = esp_timer_get_time();
  if (t1 > t0) {
    s_last_fdt_us = (uint32_t)(t1 - t0);
    if (s_validate_fdt && s_fdt_min_us > 0 && s_last_fdt_us < s_fdt_min_us) {
      ESP_LOGW(TAG, "FDT below min: %uus < %uus", (unsigned)s_last_fdt_us, (unsigned)s_fdt_min_us);
    }
  }

  bool rx_error = rx_irq.error != 0U || (rx_irq.main & ST25R3916_IRQ_MAIN_COL) != 0U;
  if (rx_wait != ESP_OK || count < rx_min || rx_error) {
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

  size_t to_read = (count > rx_max) ? rx_max : count;
  st25r3916_fifo_read(rx, to_read);
  return (int)to_read;
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

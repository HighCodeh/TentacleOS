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
      .am_mod_percent = 0, /* OOK for ISO-A */
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

/**
 * Transmit a frame and wait for the response in FIFO.
 */
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

  st25r3916_fifo_clear();

  st25r3916_fifo_set_tx_bytes((uint16_t)tx_len, 0);

  st25r3916_fifo_load(tx, tx_len);

  hb_nfc_spi_direct_cmd(with_crc ? ST25R3916_CMD_TX_WITH_CRC : ST25R3916_CMD_TX_WO_CRC);

  if (st25r3916_irq_wait_txe() != ESP_OK) {
    ESP_LOGW(TAG, "TX timeout");
    return 0;
  }

  uint16_t count = 0;
  (void)st25r3916_fifo_wait(rx_min, timeout_ms, &count);

  int64_t t1 = esp_timer_get_time();
  if (t1 > t0) {
    s_last_fdt_us = (uint32_t)(t1 - t0);
    if (s_validate_fdt && s_fdt_min_us > 0 && s_last_fdt_us < s_fdt_min_us) {
      ESP_LOGW(TAG, "FDT below min: %uus < %uus", (unsigned)s_last_fdt_us, (unsigned)s_fdt_min_us);
    }
  }

  if (count < rx_min) {
    if (count > 0) {
      size_t to_read = (count > rx_max) ? rx_max : count;
      st25r3916_fifo_read(rx, to_read);
      nfc_log_hex(" RX partial:", rx, to_read);
    }
    st25r3916_irq_log("RX fail", count);
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

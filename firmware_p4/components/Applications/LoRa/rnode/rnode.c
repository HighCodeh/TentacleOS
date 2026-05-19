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

#include "rnode.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rnode_internal.h"
#include "rnode_kiss.h"
#include "rnode_serial.h"
#include "sx1262.h"
#include "sx1262_regs.h"
#include "sx1262_types.h"

static const char *TAG = "RNODE";

#define RNODE_SERIAL_RX_CHUNK      256
#define RNODE_RX_DRAIN_BUDGET      8
#define RNODE_CAD_TIMEOUT_MS       200
#define RNODE_CSMA_RETRIES         3
#define RNODE_CSMA_BACKOFF_BASE_MS 20
#define RNODE_CSMA_BACKOFF_RAND_MS 60

static rnode_radio_cfg_t s_cfg;
static rnode_stats_t s_stats;
static rnode_kiss_decoder_t s_decoder;
static sx1262_hal_t s_hal;
static bool s_is_initialized = false;
static bool s_has_hal = false;
static uint64_t s_tx_start_us = 0;
static volatile bool s_is_cad_pending = false;
static volatile bool s_is_cad_busy = false;

static uint8_t map_sf(uint8_t sf);
static uint8_t map_bw(uint32_t bw_hz);
static uint8_t map_cr(uint8_t cr);
static sx1262_config_t build_lora_cfg(void);
static void on_radio_rx(const sx1262_packet_t *pkt, void *ctx);
static void on_radio_tx_done(void *ctx);
static void on_radio_error(int err, void *ctx);
static void on_radio_cad_done(bool is_active, void *ctx);
static bool csma_check_clear(void);
static uint32_t csma_backoff_ms(void);

rnode_radio_cfg_t *rnode_cfg_mut(void) {
  return &s_cfg;
}

rnode_stats_t *rnode_stats_mut(void) {
  return &s_stats;
}

const rnode_radio_cfg_t *rnode_get_radio_cfg(void) {
  return &s_cfg;
}

const rnode_stats_t *rnode_get_stats(void) {
  return &s_stats;
}

void rnode_send_frame(uint8_t cmd_id, const uint8_t *payload, size_t len) {
  uint8_t frame[RNODE_KISS_FRAME_MAX];
  size_t n = rnode_kiss_encode(cmd_id, payload, len, frame, sizeof(frame));
  if (n == 0) {
    ESP_LOGW(TAG, "kiss_encode overflow (cmd=0x%02X len=%u)", cmd_id, (unsigned)len);
    return;
  }
  rnode_serial_write(frame, n);
}

void rnode_send_error(uint8_t code) {
  rnode_send_frame(RNODE_CMD_ERROR, &code, 1);
}

esp_err_t rnode_apply_radio_cfg(void) {
  sx1262_config_t cfg = build_lora_cfg();
  esp_err_t ret = sx1262_config_lora(&cfg);
  if (ret != ESP_OK) {
    return ret;
  }
  if (s_cfg.is_radio_on) {
    sx1262_receive_continuous();
  }
  return ESP_OK;
}

esp_err_t rnode_set_radio_state(bool is_on) {
  if (is_on) {
    return sx1262_receive_continuous();
  }
  sx1262_stop_rx();
  return ESP_OK;
}

esp_err_t rnode_radio_transmit(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > 255) {
    return ESP_ERR_INVALID_ARG;
  }

  bool is_clear = false;
  for (int attempt = 0; attempt <= RNODE_CSMA_RETRIES; attempt++) {
    if (csma_check_clear()) {
      is_clear = true;
      break;
    }
    if (attempt < RNODE_CSMA_RETRIES) {
      vTaskDelay(pdMS_TO_TICKS(csma_backoff_ms()));
    }
  }
  if (!is_clear) {
    ESP_LOGW(TAG, "CSMA: channel busy after %d retries, dropping TX", RNODE_CSMA_RETRIES);
    if (s_cfg.is_radio_on) {
      sx1262_receive_continuous();
    }
    return ESP_ERR_TIMEOUT;
  }

  uint8_t ready = 0x01;
  rnode_send_frame(RNODE_CMD_READY, &ready, 1);

  s_tx_start_us = (uint64_t)esp_timer_get_time();
  esp_err_t ret = sx1262_transmit(data, (uint8_t)len, 5000);
  if (ret != ESP_OK) {
    if (s_cfg.is_radio_on) {
      sx1262_receive_continuous();
    }
    return ret;
  }
  uint32_t airtime_ms = rnode_airtime_estimate_ms(len, s_cfg.bw_hz, s_cfg.sf, s_cfg.cr);
  rnode_airtime_record_tx(airtime_ms);
  return ESP_OK;
}

esp_err_t rnode_init(const sx1262_hal_t *hal) {
  if (s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (hal == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(&s_cfg, 0, sizeof(s_cfg));
  memset(&s_stats, 0, sizeof(s_stats));
  rnode_kiss_decoder_reset(&s_decoder);

  s_stats.battery_pct = 100;
  s_stats.cpu_temp_c = 25;

  s_hal = *hal;
  s_has_hal = true;

  rnode_nvs_load_cfg();

  sx1262_config_t cfg = build_lora_cfg();
  esp_err_t ret = sx1262_init(&cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sx1262_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  sx1262_callbacks_t cbs = {
      .on_tx_done = on_radio_tx_done,
      .on_rx_done = on_radio_rx,
      .on_cad_done = on_radio_cad_done,
      .on_timeout = NULL,
      .on_error = on_radio_error,
      .cb_ctx = NULL,
  };
  sx1262_set_callbacks(&cbs);

  ret = rnode_serial_init();
  if (ret != ESP_OK) {
    return ret;
  }

  s_is_initialized = true;
  ESP_LOGI(TAG,
           "init done -- fw=%u.%u freq=%lu sf=%u bw=%lu cr=4/%u tx=%ddBm",
           RNODE_FW_VERSION_MAJOR,
           RNODE_FW_VERSION_MINOR,
           (unsigned long)s_cfg.freq_hz,
           s_cfg.sf,
           (unsigned long)s_cfg.bw_hz,
           s_cfg.cr,
           s_cfg.tx_power_dbm);
  return ESP_OK;
}

esp_err_t rnode_start(void) {
  if (!s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = sx1262_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sx1262_start failed: %s", esp_err_to_name(ret));
    return ret;
  }
  s_cfg.is_radio_on = true;
  return sx1262_receive_continuous();
}

void rnode_poll(void) {
  if (!s_is_initialized) {
    return;
  }

  uint8_t rx[RNODE_SERIAL_RX_CHUNK];
  int n = rnode_serial_read(rx, sizeof(rx));
  for (int i = 0; i < n; i++) {
    if (rnode_kiss_decoder_feed(&s_decoder, rx[i])) {
      uint8_t frame[RNODE_KISS_FRAME_MAX];
      size_t flen = 0;
      if (rnode_kiss_decoder_take_frame(&s_decoder, frame, sizeof(frame), &flen)) {
        rnode_dispatch(frame, flen);
      }
    }
  }

  for (int i = 0; i < RNODE_RX_DRAIN_BUDGET; i++) {
    sx1262_packet_t pkt;
    if (sx1262_get_packet(&pkt) != ESP_OK) {
      break;
    }
  }
}

static uint8_t map_sf(uint8_t sf) {
  switch (sf) {
    case 5:
      return SX1262_LORA_SF5;
    case 6:
      return SX1262_LORA_SF6;
    case 7:
      return SX1262_LORA_SF7;
    case 8:
      return SX1262_LORA_SF8;
    case 9:
      return SX1262_LORA_SF9;
    case 10:
      return SX1262_LORA_SF10;
    case 11:
      return SX1262_LORA_SF11;
    case 12:
      return SX1262_LORA_SF12;
    default:
      return SX1262_LORA_SF8;
  }
}

static uint8_t map_bw(uint32_t bw_hz) {
  if (bw_hz <= 7810)
    return SX1262_LORA_BW_7;
  if (bw_hz <= 10420)
    return SX1262_LORA_BW_10;
  if (bw_hz <= 15630)
    return SX1262_LORA_BW_15;
  if (bw_hz <= 20830)
    return SX1262_LORA_BW_20;
  if (bw_hz <= 31250)
    return SX1262_LORA_BW_31;
  if (bw_hz <= 41670)
    return SX1262_LORA_BW_41;
  if (bw_hz <= 62500)
    return SX1262_LORA_BW_62;
  if (bw_hz <= 125000)
    return SX1262_LORA_BW_125;
  if (bw_hz <= 250000)
    return SX1262_LORA_BW_250;
  return SX1262_LORA_BW_500;
}

static uint8_t map_cr(uint8_t cr) {
  switch (cr) {
    case 5:
      return SX1262_LORA_CR_4_5;
    case 6:
      return SX1262_LORA_CR_4_6;
    case 7:
      return SX1262_LORA_CR_4_7;
    case 8:
      return SX1262_LORA_CR_4_8;
    default:
      return SX1262_LORA_CR_4_5;
  }
}

static sx1262_config_t build_lora_cfg(void) {
  sx1262_config_t cfg = {
      .hal = s_hal,
      .frequency_hz = s_cfg.freq_hz,
      .sf = map_sf(s_cfg.sf),
      .bw = map_bw(s_cfg.bw_hz),
      .cr = map_cr(s_cfg.cr),
      .tx_power_dbm = s_cfg.tx_power_dbm,
      .preamble_len = 12,
      .is_crc_on = true,
      .is_inverted_iq = false,
      .is_implicit_hdr = false,
      .is_public_network = false,
  };
  return cfg;
}

static void on_radio_rx(const sx1262_packet_t *pkt, void *ctx) {
  (void)ctx;
  if (pkt == NULL || pkt->len == 0) {
    return;
  }
  if (pkt->has_crc_error || pkt->has_header_error) {
    return;
  }

  s_stats.rx_count++;
  s_stats.last_rssi_dbm = pkt->rssi_pkt_dbm;
  s_stats.last_snr_quarter_db = pkt->snr_pkt_db;

  rnode_send_frame(RNODE_CMD_DATA, pkt->buf, pkt->len);

  int rssi = pkt->rssi_pkt_dbm;
  if (rssi < -157)
    rssi = -157;
  if (rssi > 98)
    rssi = 98;
  uint8_t rssi_byte = (uint8_t)(rssi + RNODE_RSSI_OFFSET);
  rnode_send_frame(RNODE_CMD_STAT_RSSI, &rssi_byte, 1);

  uint8_t snr_byte = (uint8_t)pkt->snr_pkt_db;
  rnode_send_frame(RNODE_CMD_STAT_SNR, &snr_byte, 1);

  uint8_t rxc[4] = {
      (uint8_t)((s_stats.rx_count >> 24) & 0xFF),
      (uint8_t)((s_stats.rx_count >> 16) & 0xFF),
      (uint8_t)((s_stats.rx_count >> 8) & 0xFF),
      (uint8_t)(s_stats.rx_count & 0xFF),
  };
  rnode_send_frame(RNODE_CMD_STAT_RX, rxc, sizeof(rxc));
}

static void on_radio_tx_done(void *ctx) {
  (void)ctx;
  if (s_tx_start_us > 0) {
    s_tx_start_us = 0;
  }
  s_stats.tx_count++;

  uint8_t txc[4] = {
      (uint8_t)((s_stats.tx_count >> 24) & 0xFF),
      (uint8_t)((s_stats.tx_count >> 16) & 0xFF),
      (uint8_t)((s_stats.tx_count >> 8) & 0xFF),
      (uint8_t)(s_stats.tx_count & 0xFF),
  };
  rnode_send_frame(RNODE_CMD_STAT_TX, txc, sizeof(txc));

  if (s_cfg.is_radio_on) {
    sx1262_receive_continuous();
  }
}

static void on_radio_error(int err, void *ctx) {
  (void)ctx;
  ESP_LOGW(TAG, "radio error: %d", err);
}

static void on_radio_cad_done(bool is_active, void *ctx) {
  (void)ctx;
  s_is_cad_busy = is_active;
  s_is_cad_pending = false;
}

static bool csma_check_clear(void) {
  s_is_cad_pending = true;
  s_is_cad_busy = false;

  sx1262_stop_rx();
  esp_err_t err = sx1262_cad_start();
  if (err != ESP_OK) {
    s_is_cad_pending = false;
    return true;
  }

  uint32_t deadline_ms = (uint32_t)(esp_timer_get_time() / 1000ULL) + RNODE_CAD_TIMEOUT_MS;
  while (s_is_cad_pending) {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if ((int32_t)(now_ms - deadline_ms) >= 0) {
      s_is_cad_pending = false;
      ESP_LOGW(TAG, "CAD timeout -- assume clear");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return !s_is_cad_busy;
}

static uint32_t csma_backoff_ms(void) {
  uint32_t r = esp_random() % RNODE_CSMA_BACKOFF_RAND_MS;
  return RNODE_CSMA_BACKOFF_BASE_MS + r;
}

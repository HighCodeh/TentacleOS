// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sx1262_airtime.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "sx1262_regs.h"

#define AIRTIME_US_PER_SECOND             1000000ULL
#define AIRTIME_HOUR_US                   (3600ULL * AIRTIME_US_PER_SECOND)
#define AIRTIME_LDRO_MAX_BW_HZ            125000U
#define AIRTIME_BYTE_BITS                 8
#define AIRTIME_CRC_BITS                  16
#define AIRTIME_HEADER_BITS               20
#define AIRTIME_SF_BITS                   4
#define AIRTIME_HEADER_EXTRA_BITS         8
#define AIRTIME_CODING_BASE               4
#define AIRTIME_PREAMBLE_EXTRA_SYMBOLS    12
#define AIRTIME_SHORT_SF_EXTRA_SYMBOLS    2
#define AIRTIME_SYMBOL_QUARTERS           4
#define AIRTIME_LDRO_SF_OFFSET            2
#define AIRTIME_EU433_LOW_HZ              433050000U
#define AIRTIME_EU433_HIGH_HZ             434790000U
#define AIRTIME_EU868_LOW_HZ              863000000U
#define AIRTIME_EU868_HIGH_HZ             870000000U
#define AIRTIME_EU868_ONE_LOW_HZ          865000000U
#define AIRTIME_EU868_ONE_HIGH_HZ         868600000U
#define AIRTIME_EU868_TEN_LOW_HZ          869400000U
#define AIRTIME_EU868_TEN_HIGH_HZ         869650000U
#define AIRTIME_TEN_PERCENT_DIVISOR       10U
#define AIRTIME_ONE_PERCENT_DIVISOR       100U
#define AIRTIME_POINT_ONE_PERCENT_DIVISOR 1000U

static const char *TAG = "SX1262_AIRTIME";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_next_tx_us = 0;

static uint32_t duty_divisor(const sx1262_config_t *config, uint32_t bandwidth);

uint64_t sx1262_airtime_estimate_us(const sx1262_config_t *config, uint8_t length) {
  if (config == NULL || length == 0 || config->sf < SX1262_LORA_SF5 ||
      config->sf > SX1262_LORA_SF12 || config->cr < SX1262_LORA_CR_4_5 ||
      config->cr > SX1262_LORA_CR_4_8) {
    return 0;
  }
  uint32_t bandwidth = sx1262_bw_to_hz(config->bw);
  if (bandwidth == 0) {
    return 0;
  }
  int32_t bits = length * AIRTIME_BYTE_BITS + (config->is_crc_on ? AIRTIME_CRC_BITS : 0) -
                 AIRTIME_SF_BITS * config->sf + (config->is_implicit_hdr ? 0 : AIRTIME_HEADER_BITS);
  uint32_t denominator = AIRTIME_SF_BITS * config->sf;
  if (config->sf > SX1262_LORA_SF6) {
    bits += AIRTIME_HEADER_EXTRA_BITS;
    if (config->sf >= SX1262_LORA_SF11 && bandwidth <= AIRTIME_LDRO_MAX_BW_HZ) {
      denominator = AIRTIME_SF_BITS * (config->sf - AIRTIME_LDRO_SF_OFFSET);
    }
  }
  uint32_t symbols = config->preamble_len + AIRTIME_PREAMBLE_EXTRA_SYMBOLS;
  if (bits > 0) {
    symbols +=
        ((uint32_t)bits + denominator - 1) / denominator * (config->cr + AIRTIME_CODING_BASE);
  }
  if (config->sf <= SX1262_LORA_SF6) {
    symbols += AIRTIME_SHORT_SF_EXTRA_SYMBOLS;
  }
  uint64_t numerator = (uint64_t)(AIRTIME_SYMBOL_QUARTERS * symbols + 1) *
                       (1U << (config->sf - AIRTIME_LDRO_SF_OFFSET));
  return (numerator * AIRTIME_US_PER_SECOND + bandwidth - 1) / bandwidth;
}

esp_err_t sx1262_airtime_reserve(const sx1262_config_t *config, uint8_t length) {
  uint64_t airtime = sx1262_airtime_estimate_us(config, length);
  if (airtime == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  uint32_t divisor = duty_divisor(config, sx1262_bw_to_hz(config->bw));
  if (divisor == 0) {
    return ESP_OK;
  }
  if (airtime > AIRTIME_HOUR_US / divisor) {
    return ESP_ERR_INVALID_SIZE;
  }
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  bool can_transmit = now >= s_next_tx_us;
  if (can_transmit) {
    s_next_tx_us = now + (int64_t)(airtime * divisor);
  }
  portEXIT_CRITICAL(&s_lock);
  if (!can_transmit) {
    ESP_LOGD(TAG, "TX deferred by shared airtime budget");
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

static uint32_t duty_divisor(const sx1262_config_t *config, uint32_t bandwidth) {
  uint32_t low = config->frequency_hz - bandwidth / 2;
  uint32_t high = config->frequency_hz + (bandwidth + 1) / 2;
  if (low <= AIRTIME_EU433_HIGH_HZ && high >= AIRTIME_EU433_LOW_HZ) {
    return AIRTIME_TEN_PERCENT_DIVISOR;
  }
  if (low > AIRTIME_EU868_HIGH_HZ || high < AIRTIME_EU868_LOW_HZ) {
    return 0;
  }
  if (low >= AIRTIME_EU868_TEN_LOW_HZ && high <= AIRTIME_EU868_TEN_HIGH_HZ) {
    return AIRTIME_TEN_PERCENT_DIVISOR;
  }
  if (low >= AIRTIME_EU868_ONE_LOW_HZ && high <= AIRTIME_EU868_ONE_HIGH_HZ) {
    return AIRTIME_ONE_PERCENT_DIVISOR;
  }
  return AIRTIME_POINT_ONE_PERCENT_DIVISOR;
}

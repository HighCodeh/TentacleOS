// Copyright (c) 2026 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef RNODE_H
#define RNODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#include "sx1262.h"

#define RNODE_FW_VERSION_MAJOR 1
#define RNODE_FW_VERSION_MINOR 52

#define RNODE_PLATFORM_ESP32 0x80
#define RNODE_MCU_ESP32      0x81

#define RNODE_DEFAULT_FREQ_HZ      915000000U
#define RNODE_DEFAULT_BW_HZ        125000U
#define RNODE_DEFAULT_SF           8
#define RNODE_DEFAULT_CR           5
#define RNODE_DEFAULT_TX_POWER_DBM 17

/**
 * @brief Current radio configuration applied by the host over KISS.
 */
typedef struct {
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
  int8_t tx_power_dbm;
  bool is_radio_on;
} rnode_radio_cfg_t;

/**
 * @brief Telemetry state reported to the host.
 */
typedef struct {
  uint32_t rx_count;
  uint32_t tx_count;
  int16_t last_rssi_dbm;
  int8_t last_snr_quarter_db;
  uint8_t battery_pct;
  int8_t cpu_temp_c;
} rnode_stats_t;

/**
 * @brief Initialize the RNode subsystem (KISS engine + serial + SX1262 hookup).
 *
 * @param hal  SX1262 HAL previously created via sx1262_hal_create.
 * @return ESP_OK on success.
 */
esp_err_t rnode_init(const sx1262_hal_t *hal);

/**
 * @brief Start radio IRQ task and continuous RX.
 *
 * @return ESP_OK on success.
 */
esp_err_t rnode_start(void);

/**
 * @brief Periodic service. Call from the main loop. Drains serial RX,
 *        runs the KISS decoder and dispatches command frames.
 */
void rnode_poll(void);

/**
 * @brief Read-only snapshot of the current radio configuration.
 */
const rnode_radio_cfg_t *rnode_get_radio_cfg(void);

/**
 * @brief Read-only snapshot of the telemetry counters.
 */
const rnode_stats_t *rnode_get_stats(void);

#ifdef __cplusplus
}
#endif

#endif // RNODE_H

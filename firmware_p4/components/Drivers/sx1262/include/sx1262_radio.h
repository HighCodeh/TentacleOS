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

#ifndef SX1262_RADIO_H
#define SX1262_RADIO_H

#include "esp_err.h"
#include "sx1262_types.h"
#include "sx1262_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize radio subsystem with HAL and config pointers.
 */
void sx1262_radio_init(sx1262_hal_t *hal, sx1262_config_t *config);

/**
 * @brief Transmit data via LoRa. Applies W1 before each TX.
 *
 * Non-blocking: returns after SetTx command. Completion via TxDone IRQ.
 *
 * @param data        Payload bytes. Must not be NULL.
 * @param len         Payload length (1–255).
 * @param timeout_ms  TX timeout in ms. 0 = no timeout.
 * @return ESP_OK on success.
 */
esp_err_t sx1262_radio_transmit(const uint8_t *data, uint8_t len, uint32_t timeout_ms);

/**
 * @brief Start single RX. Chip returns to STDBY after one packet or timeout.
 *
 * @param timeout_ms  0 = no timeout (single, wait forever). >0 = timeout in ms.
 * @return ESP_OK on success.
 */
esp_err_t sx1262_radio_receive_single(uint32_t timeout_ms);

/**
 * @brief Start continuous RX. Chip stays in RX until stop_rx().
 * @return ESP_OK on success.
 */
esp_err_t sx1262_radio_receive_continuous(void);

/**
 * @brief Stop RX and return to STDBY_RC.
 */
void sx1262_radio_stop_rx(void);
/* Shielded laboratory service only; caller must enforce a finite CW deadline. */
esp_err_t sx1262_radio_lab_cw_start(void);
esp_err_t sx1262_radio_standby_checked(void);

/**
 * @brief Start Channel Activity Detection.
 * Result via on_cad_done callback (is_channel_active).
 * CAD params adjusted per SF — DS Table 13-71.
 */
esp_err_t sx1262_radio_cad_start(void);

/**
 * @brief Read instantaneous RSSI. DS Section 13.5.4.
 * @param out_rssi_dbm  Result in dBm. Must not be NULL.
 */
esp_err_t sx1262_radio_get_rssi_inst(int16_t *out_rssi_dbm);

/**
 * @brief Read packet statistics. DS Section 13.5.5.
 */
esp_err_t sx1262_radio_get_stats(sx1262_stats_t *out_stats);

/**
 * @brief Reset packet statistics. DS Section 13.5.6.
 */
esp_err_t sx1262_radio_reset_stats(void);

/**
 * @brief Enter sleep mode. DS Section 9.3, Table 13-1.
 *
 * @param is_warm  true = warm start (retain config), false = cold start (needs re-init).
 * @return ESP_OK on success.
 */
esp_err_t sx1262_radio_sleep(bool is_warm);

/**
 * @brief Wake from sleep and enter STDBY_RC. DS Section 9.3.
 *
 * Toggles NSS to wake chip, waits for BUSY LOW, sends SetStandby(RC).
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not in SLEEP.
 */
esp_err_t sx1262_radio_wakeup(void);

/**
 * @brief Start RX duty cycle (wake-on-radio). DS Table 13-12.
 *
 * Chip alternates RX/sleep automatically. 1 tick = 15.625 us.
 *
 * @param rx_ms     RX window in ms. Must be > 0.
 * @param sleep_ms  Sleep window in ms.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if rx_ms == 0.
 */
esp_err_t sx1262_radio_set_rx_duty_cycle(uint32_t rx_ms, uint32_t sleep_ms);

#ifdef __cplusplus
}
#endif

#endif // SX1262_RADIO_H

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

#ifndef SX1262_H
#define SX1262_H

#include "esp_err.h"
#include "sx1262_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the SX1262 driver.
 *
 * Validates HAL callbacks and config, performs hardware reset,
 * calibration, workarounds, and configures the radio.
 * DS Section 14.5 init sequence.
 *
 * @param config  Pointer to configuration (includes HAL). Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config/HAL invalid.
 */
esp_err_t sx1262_init(const sx1262_config_t *config);

/**
 * @brief Tear down the SX1262 driver so the next sx1262_init() starts clean.
 *
 * Stops the IRQ task (if running), destroys the HAL (removes the SPI device
 * and mutex; SPI3 bus is preserved for shared peripherals), and clears
 * driver state.
 *
 * Safe to call when nothing is initialized — no-op.
 *
 * @return ESP_OK on success.
 */
esp_err_t sx1262_deinit(void);

/**
 * @brief Start the IRQ processing task.
 *
 * Creates the ISR task and enables DIO1 interrupt.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running.
 */
esp_err_t sx1262_start(void);

/**
 * @brief Stop the IRQ processing task gracefully.
 *
 * Waits for task to exit via task notification. Safe to call if not running.
 */
void sx1262_stop(void);

/**
 * @brief Reconfigure LoRa parameters at runtime.
 *
 * Validates all parameters before any SPI transaction.
 * Applies workaround W4 (IQ polarity) on every call.
 *
 * @param config  Pointer to new configuration. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if parameters invalid.
 */
esp_err_t sx1262_config_lora(const sx1262_config_t *config);

/**
 * @brief Register event callbacks.
 *
 * @param cbs  Pointer to callback struct. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if cbs is NULL.
 */
esp_err_t sx1262_set_callbacks(const sx1262_callbacks_t *cbs);

/* ── FSM ──────────────────────────────────────────────────────────── */

/**
 * @brief Get current FSM state.
 *
 * @return Current state (sx1262_state_t).
 */
sx1262_state_t sx1262_get_state(void);

/* ── Status ───────────────────────────────────────────────────────── */

/**
 * @brief Check if the IRQ processing task is running.
 *
 * @return true if running, false otherwise.
 */
bool sx1262_is_running(void);

/**
 * @brief Read chip status byte. DS Section 13.5.1.
 *
 * @param out_status  Pointer to receive status byte. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_status is NULL.
 */
esp_err_t sx1262_get_status(uint8_t *out_status);

/**
 * @brief Read device error bitmask. DS Section 13.5.7.
 *
 * @param out_errors  Pointer to receive error bitmask. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_errors is NULL.
 */
esp_err_t sx1262_get_device_errors(uint16_t *out_errors);

/**
 * @brief Read instantaneous RSSI of the channel. DS Section 13.5.4.
 *
 * @param out_rssi_dbm  Pointer to receive RSSI in dBm. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_rssi_dbm is NULL.
 */
esp_err_t sx1262_get_rssi_inst(int16_t *out_rssi_dbm);

/**
 * @brief Read packet statistics (received, CRC errors, header errors).
 *        DS Section 13.5.5.
 *
 * @param out_stats  Pointer to receive statistics. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_stats is NULL.
 */
esp_err_t sx1262_get_stats(sx1262_stats_t *out_stats);

/* ── TX ───────────────────────────────────────────────────────────── */

/**
 * @brief Transmit data via LoRa. Non-blocking.
 *
 * Applies workaround W1 (BW500 sensitivity) before each TX.
 * Completion notified via on_tx_done callback.
 *
 * @param data        Payload bytes. Must not be NULL.
 * @param len         Payload length (1-255). 0 returns ESP_ERR_INVALID_ARG.
 * @param timeout_ms  TX timeout in ms. 0 = no timeout.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if data is NULL or len is 0.
 */
esp_err_t sx1262_transmit(const uint8_t *data, uint8_t len, uint32_t timeout_ms);

/* ── RX ───────────────────────────────────────────────────────────── */

/**
 * @brief Start single RX. Returns to STDBY after one packet or timeout.
 *
 * @param timeout_ms  0 = no timeout (wait forever). >0 = timeout in ms.
 * @return ESP_OK on success.
 */
esp_err_t sx1262_receive_single(uint32_t timeout_ms);

/**
 * @brief Start continuous RX. Stays in RX until sx1262_stop_rx() is called.
 *
 * @return ESP_OK on success.
 */
esp_err_t sx1262_receive_continuous(void);

/**
 * @brief Stop RX and return to STDBY_RC.
 */
void sx1262_stop_rx(void);

/**
 * @brief Dequeue next received packet from the ring buffer.
 *
 * Thread-safe via enter_critical/exit_critical.
 *
 * @param out_packet  Destination for packet data. Must not be NULL.
 * @return ESP_OK if packet available, ESP_ERR_NOT_FOUND if empty.
 */
esp_err_t sx1262_get_packet(sx1262_packet_t *out_packet);

/* ── CAD & Power ──────────────────────────────────────────────────── */

/**
 * @brief Start Channel Activity Detection. DS Section 13.4.7.
 *
 * CAD parameters adjusted per SF. Result via on_cad_done callback.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if in TX or UNKNOWN.
 */
esp_err_t sx1262_cad_start(void);

/**
 * @brief Enter sleep mode. DS Section 9.3.
 *
 * @param is_warm  true = warm start (retain config), false = cold start.
 *                 Cold start sets s_needs_reinit — TX/RX require re-init.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if transition not allowed.
 */
esp_err_t sx1262_sleep(bool is_warm);

/**
 * @brief Wake from sleep and enter STDBY_RC. DS Section 9.3.
 *
 * Toggles NSS LOW to wake chip, waits for BUSY, sends SetStandby(RC).
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not in SLEEP.
 */
esp_err_t sx1262_wakeup(void);

/**
 * @brief Start RX duty cycle (wake-on-radio). DS Table 13-12.
 *
 * Chip alternates between RX and sleep automatically.
 * Period conversion: ticks = ms * 64 (1 tick = 15.625 us).
 *
 * @param rx_ms     RX window in ms. Must be > 0.
 * @param sleep_ms  Sleep window in ms.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if rx_ms is 0.
 */
esp_err_t sx1262_set_rx_duty_cycle(uint32_t rx_ms, uint32_t sleep_ms);

/* ── IRQ Processing ──────────────────────────────────────────────── */

/**
 * @brief Process pending IRQ flags from the SX1262.
 *
 * Called by ISR task when DIO1 rises. Never call directly from ISR.
 * Sequence: GetIrqStatus -> ClearIrqStatus -> dispatch -> callbacks -> FSM.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t sx1262_process_irq(void);

#ifdef __cplusplus
}
#endif

#endif // SX1262_H

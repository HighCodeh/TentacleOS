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

#ifndef IR_H
#define IR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/rmt_types.h"

#include "ir_protocol.h"
#include "pin_def.h" // GPIO_IR_TX_PIN / GPIO_IR_RX_PIN — board pin map (was hardcoded 5/6 here,
                     // which fought the schematic remap in pin_def.h: TX=0, RX=1)

#define IR_RMT_RESOLUTION_HZ  1000000
#define IR_RMT_MEM_SYMBOLS    128
#define IR_MAX_SYMBOLS        512
// Non-DMA RX FIFO depth: 2x the P4's 48-word-per-channel block. Long frames
// ping-pong into the IR_MAX_SYMBOLS user buffer. (DMA RX on this board failed at
// rmt_receive() and the old error paths leaked the channel — see ir_rx_init.)
#define IR_RX_MEM_BLOCK_SYMBOLS 96
// TX channel FIFO: 64 symbols = 2 P4 channels (was 128 = 3, which nearly
// exhausts the 4 TX channels alongside the WS2812 LED strip). rmt_transmit()
// ping-pongs longer frames; the encode buffer stays IR_RMT_MEM_SYMBOLS.
#define IR_TX_MEM_BLOCK_SYMBOLS 64
#define IR_RX_MIN_NS          1250
#define IR_RX_MAX_NS          12000000
#define IR_TX_QUEUE_DEPTH     4
#define IR_CARRIER_DUTY_CYCLE 0.33f
#define IR_TX_WAIT_MS         1000
#define IR_PRINT_MAX_SYMBOLS  40

/**
 * @brief Initialize the IR RX channel.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NO_MEM if mutex or queue creation fails
 *   - Other ESP_ERR codes from the RMT driver
 */
esp_err_t ir_rx_init(void);

/**
 * @brief Prepare the RX channel for a fresh capture: drop any stale/ambient frame
 *        and re-arm the one-shot receive if a previous frame left it idle.
 *
 * Call this at the start of every capture. Without it, a frame arriving between
 * captures (e.g. an NEC repeat frame) consumes the armed receive without re-arming,
 * so the next ir_receive() would wait forever.
 */
void ir_rx_prime(void);

/**
 * @brief Release the IR RX channel and its resources.
 *
 * Tears down what ir_rx_init() allocated (RMT channel + event queue) so the RMT
 * channel is freed instead of staying held for the rest of the session. Safe to
 * call when RX was never inited (no-op). ir_rx_init() re-initializes afterwards.
 */
void ir_rx_deinit(void);

/**
 * @brief Initialize the IR TX channel.
 *
 * @return
 *   - ESP_OK on success
 *   - Other ESP_ERR codes from the RMT driver
 */
esp_err_t ir_tx_init(void);

/**
 * @brief Release the IR TX channel and its resources.
 *
 * Tears down what ir_tx_init() allocated (RMT channel + copy encoder) so the RMT
 * channel is freed instead of staying held for the rest of the session. Safe to
 * call when TX was never inited (no-op). ir_tx_init() re-initializes afterwards.
 */
void ir_tx_deinit(void);

/**
 * @brief Drive the IR emitter fully on (DC, no carrier) at maximum power.
 *
 * Torch mode: bypasses the RMT carrier and holds GPIO_IR_TX_PIN high so the LED
 * stays continuously lit instead of pulsing at the 38 kHz carrier duty. Releases
 * the RMT TX channel first (ir_tx_deinit) so it does not fight for the pin. The
 * emitter is infrared, so it is invisible to the eye - verify with a phone camera.
 * Continuous full-power drive: use for tests/short bursts, do not leave on for long.
 *
 * @return ESP_OK on success, or an ESP_ERR code from the GPIO driver.
 */
esp_err_t ir_flash_on(void);

/**
 * @brief Turn the IR emitter off after ir_flash_on() (drives GPIO_IR_TX_PIN low).
 *
 * @return ESP_OK on success, or an ESP_ERR code from the GPIO driver.
 */
esp_err_t ir_flash_off(void);

/**
 * @brief Wait for and decode an incoming IR frame.
 *
 * @param[out] out_data    Destination for the decoded IR data. Must not be NULL.
 * @param[in]  timeout_ms  Maximum time to wait, in milliseconds.
 *
 * @return
 *   - ESP_OK              Frame received and decoded successfully
 *   - ESP_ERR_TIMEOUT     No frame received within @p timeout_ms
 *   - ESP_ERR_NOT_FOUND   Frame received but decoding failed
 *   - Other ESP_ERR codes from the RMT driver
 */
esp_err_t ir_receive(ir_data_t *out_data, uint32_t timeout_ms);

/**
 * @brief Encode and transmit an IR command.
 *
 * @param[in] data  IR command to transmit. Must not be NULL.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if encoding produces no symbols
 *   - Other ESP_ERR codes from the RMT driver
 */
esp_err_t ir_send(const ir_data_t *data);

/**
 * @brief Transmit a raw sequence of RMT symbols.
 *
 * @param[in] symbols     Array of RMT symbols to transmit. Must not be NULL.
 * @param[in] count       Number of symbols in @p symbols.
 * @param[in] carrier_hz  Carrier frequency in Hz. Pass 0 to disable carrier.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if symbols is NULL or count is 0
 *   - Other ESP_ERR codes from the RMT driver
 */
esp_err_t ir_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz);

/**
 * @brief Copy the raw RMT symbols from the last received frame.
 *
 * @param[out] out_buf    Caller-supplied buffer to receive the symbols.
 * @param      buf_max    Capacity of out_buf in symbols.
 * @param[out] out_count  Number of symbols copied. May be NULL.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if out_buf is NULL or buf_max is 0
 *   - ESP_ERR_TIMEOUT if mutex could not be acquired
 */
esp_err_t ir_get_last_raw(rmt_symbol_word_t *out_buf, size_t buf_max, size_t *out_count);

/**
 * @brief Log raw RMT symbols at DEBUG level.
 *
 * @param[in] symbols  Array of RMT symbols to print. Must not be NULL.
 * @param[in] count    Number of symbols in @p symbols.
 */
void ir_print_raw(const rmt_symbol_word_t *symbols, size_t count);

/**
 * @brief Log decoded IR data at INFO level.
 *
 * @param[in] data  Decoded IR data to print. Must not be NULL.
 */
void ir_print_data(const ir_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // IR_H
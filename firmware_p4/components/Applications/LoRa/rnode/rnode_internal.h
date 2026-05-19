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

#ifndef RNODE_INTERNAL_H
#define RNODE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rnode.h"

#define RNODE_CMD_DATA        0x00
#define RNODE_CMD_FREQUENCY   0x01
#define RNODE_CMD_BANDWIDTH   0x02
#define RNODE_CMD_TXPOWER     0x03
#define RNODE_CMD_SF          0x04
#define RNODE_CMD_CR          0x05
#define RNODE_CMD_RADIO_STATE 0x06
#define RNODE_CMD_RADIO_LOCK  0x07
#define RNODE_CMD_DETECT      0x08
#define RNODE_CMD_READY       0x0F
#define RNODE_CMD_LEAVE       0x0A
#define RNODE_CMD_ST_ALOCK    0x0B
#define RNODE_CMD_LT_ALOCK    0x0C
#define RNODE_CMD_STAT_RX     0x21
#define RNODE_CMD_STAT_TX     0x22
#define RNODE_CMD_STAT_RSSI   0x23
#define RNODE_CMD_STAT_SNR    0x24
#define RNODE_CMD_STAT_CHTM   0x25
#define RNODE_CMD_STAT_PHYPRM 0x26
#define RNODE_CMD_STAT_BAT    0x27
#define RNODE_CMD_STAT_CSMA   0x28
#define RNODE_CMD_STAT_TEMP   0x29
#define RNODE_CMD_BLINK       0x30
#define RNODE_CMD_RANDOM      0x40
#define RNODE_CMD_DETECT_RESP 0x46
#define RNODE_CMD_PLATFORM    0x48
#define RNODE_CMD_MCU         0x49
#define RNODE_CMD_FW_VERSION  0x50
#define RNODE_CMD_RESET       0x55
#define RNODE_CMD_ERROR       0x90

#define RNODE_DETECT_PROBE 0x73
#define RNODE_RESET_MAGIC  0xF8
#define RNODE_LEAVE_MAGIC  0xFF

#define RNODE_ERROR_INITRADIO     0x01
#define RNODE_ERROR_TXFAILED      0x02
#define RNODE_ERROR_EEPROM_LOCKED 0x03
#define RNODE_ERROR_QUEUE_FULL    0x04
#define RNODE_ERROR_MEMORY_LOW    0x05
#define RNODE_ERROR_MODEM_TIMEOUT 0x06

#define RNODE_RSSI_OFFSET 157
#define RNODE_TEMP_OFFSET 120

#define RNODE_AIRTIME_WINDOW_MS 3600000UL

/**
 * @brief Encode a KISS frame and send it over the serial port.
 */
void rnode_send_frame(uint8_t cmd_id, const uint8_t *payload, size_t len);

/**
 * @brief Send an error frame (CMD_ERROR + 1B code).
 */
void rnode_send_error(uint8_t code);

/**
 * @brief Central command dispatcher. Processes a decoded KISS frame.
 */
void rnode_dispatch(const uint8_t *frame, size_t len);

/**
 * @brief Push the current radio config to the SX1262 (freq/bw/sf/cr/power).
 */
esp_err_t rnode_apply_radio_cfg(void);

/**
 * @brief Toggle continuous RX on the SX1262.
 */
esp_err_t rnode_set_radio_state(bool is_on);

/**
 * @brief Transmit payload via SX1262 with CSMA/CAD listen-before-talk.
 */
esp_err_t rnode_radio_transmit(const uint8_t *data, size_t len);

/**
 * @brief Mutable accessors for internal config and stats.
 */
rnode_radio_cfg_t *rnode_cfg_mut(void);
rnode_stats_t *rnode_stats_mut(void);

/**
 * @brief Persist the current radio config in NVS.
 */
void rnode_nvs_save_cfg(void);

/**
 * @brief Load radio config from NVS, falling back to defaults.
 */
void rnode_nvs_load_cfg(void);

/**
 * @brief Record a TX event in the airtime tracker.
 */
void rnode_airtime_record_tx(uint32_t airtime_ms);

/**
 * @brief Accumulated airtime in the short window (1 min) as hundredths of %.
 */
uint16_t rnode_airtime_short(void);

/**
 * @brief Accumulated airtime in the long window (1 hour) as hundredths of %.
 */
uint16_t rnode_airtime_long(void);

/**
 * @brief Regulatory setpoints (host sends via CMD_ST_ALOCK / CMD_LT_ALOCK).
 *        Values in hundredths of % (0..10000).
 */
void rnode_airtime_set_limits(uint16_t st_limit_cpct, uint16_t lt_limit_cpct);

/**
 * @brief Returns true if TX is currently blocked by airtime enforcement.
 */
bool rnode_airtime_is_blocked(void);

/**
 * @brief Estimate LoRa packet airtime in milliseconds.
 *        Semtech AN1200.13 formula.
 */
uint32_t rnode_airtime_estimate_ms(size_t payload_bytes, uint32_t bw_hz, uint8_t sf, uint8_t cr);

#ifdef __cplusplus
}
#endif

#endif // RNODE_INTERNAL_H

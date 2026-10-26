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

#ifndef LORA_LAB_H
#define LORA_LAB_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  LORA_LAB_IDLE = 0,
  LORA_LAB_STARTING,
  LORA_LAB_RX,
  LORA_LAB_FAILED,
} lora_lab_state_t;

typedef struct {
  lora_lab_state_t state;
  esp_err_t error;
  uint32_t frequency_hz;
  uint8_t chip_status;
  uint16_t device_errors;
  int busy_level;
  int dio1_level;
  int16_t rssi_now_dbm;
  int16_t rssi_min_dbm;
  int16_t rssi_max_dbm;
  int16_t rssi_avg_dbm;
  uint32_t rssi_samples;
  uint32_t rx_packets;
  uint16_t crc_errors;
  uint16_t header_errors;
  int16_t last_packet_rssi_dbm;
  int8_t last_packet_snr_db;
  uint8_t last_packet_len;
  uint8_t last_packet_prefix[12];
} lora_lab_snapshot_t;

/**
 * Start a receive-only SX1262 engineering session (SF7/BW125/CR4/5,
 * private sync word). The worker performs SPI/chip validation, samples
 * instantaneous RSSI, and records real LoRa packets. It never transmits.
 */
esp_err_t lora_lab_start(uint32_t frequency_hz);

/** Stop the lab worker, return the radio to idle, and release its power lock. */
void lora_lab_stop(void);

/** True while the worker owns or is bringing up the SX1262. */
bool lora_lab_active(void);

/** Copy a coherent snapshot for UI or diagnostics. */
void lora_lab_get_snapshot(lora_lab_snapshot_t *out);

/** Reset accumulated RSSI and packet counters without stopping RX. */
void lora_lab_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_LAB_H

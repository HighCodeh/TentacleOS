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

#ifndef RF_CHAMBER_H
#define RF_CHAMBER_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RF_CHAMBER_PAYLOAD_SIZE 32U
#define RF_CHAMBER_MAX_MS       120000U
#define RF_CHAMBER_MAX_CW_MS    10000U
typedef enum { RF_CHAMBER_CC, RF_CHAMBER_SX, RF_CHAMBER_RADIO_COUNT } rf_chamber_radio_t;
typedef enum {
  RF_CHAMBER_CW,
  RF_CHAMBER_TX,
  RF_CHAMBER_RX,
  RF_CHAMBER_MODE_COUNT
} rf_chamber_mode_t;
typedef enum {
  RF_CHAMBER_IDLE,
  RF_CHAMBER_STARTING,
  RF_CHAMBER_RUNNING,
  RF_CHAMBER_STOPPING,
  RF_CHAMBER_HELD,
  RF_CHAMBER_PHASE_COUNT
} rf_chamber_phase_t;
typedef struct {
  rf_chamber_radio_t radio;
  rf_chamber_mode_t mode;
  uint32_t frequency_hz;
  int8_t power_dbm;
  uint8_t path;
  uint8_t sf;
  uint16_t bandwidth_khz;
  uint8_t cr;
  uint32_t duration_ms;
  uint32_t interval_ms;
} rf_chamber_config_t;
typedef struct {
  rf_chamber_config_t config;
  rf_chamber_phase_t phase;
  uint32_t run_id;
  uint32_t elapsed_ms;
  uint32_t tx_submitted;
  uint32_t tx_done;
  uint32_t tx_timeout;
  uint32_t duty_deferred;
  uint32_t rx_test;
  uint32_t rx_other;
  uint32_t rx_error;
  uint32_t sequence_last;
  uint32_t sequence_gaps;
  uint32_t sequence_repeats;
  uint32_t samples;
  int32_t rssi_sum_half_dbm;
  int16_t rssi_min_half_dbm;
  int16_t rssi_max_half_dbm;
  int8_t last_snr_db;
  int8_t frequency_error_raw;
  uint8_t radio_status;
  uint8_t pa_code;
  uint16_t device_errors;
  esp_err_t error;
  esp_err_t cleanup_error;
  bool stopped_by_user;
} rf_chamber_status_t;
esp_err_t rf_chamber_validate(const rf_chamber_config_t *config);
void rf_chamber_payload(uint32_t sequence, uint8_t *out_payload);
bool rf_chamber_parse_payload(const uint8_t *data, uint8_t length, uint32_t *out_sequence);
esp_err_t rf_chamber_start(const rf_chamber_config_t *config);
void rf_chamber_stop(void);
esp_err_t rf_chamber_mesh_stop_async(void);
esp_err_t rf_chamber_mesh_stop_status(bool *out_busy);
void rf_chamber_snapshot(rf_chamber_status_t *out_status);
#ifdef __cplusplus
}
#endif
#endif

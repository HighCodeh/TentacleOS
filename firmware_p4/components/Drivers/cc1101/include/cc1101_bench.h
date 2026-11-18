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

#ifndef CC1101_BENCH_H
#define CC1101_BENCH_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CC1101_BENCH_PAYLOAD_SIZE 32U
typedef struct {
  uint32_t frequency_hz;
  int8_t power_dbm;
  uint8_t path;
  bool is_cw;
} cc1101_bench_config_t;
typedef struct {
  uint8_t state;
  uint8_t pa;
  int16_t rssi_half_dbm;
  int8_t frequency_error;
  bool has_packet;
  bool has_crc_error;
  uint8_t payload[CC1101_BENCH_PAYLOAD_SIZE];
} cc1101_bench_sample_t;
/* Caller owns the persistent CC1101 lease through checked restore. */
esp_err_t cc1101_bench_prepare(const cc1101_bench_config_t *config);
esp_err_t cc1101_bench_enter(bool is_tx);
esp_err_t cc1101_bench_packet(const uint8_t *payload);
esp_err_t cc1101_bench_sample(cc1101_bench_sample_t *out_sample);
esp_err_t cc1101_bench_restore(void);
#ifdef __cplusplus
}
#endif
#endif

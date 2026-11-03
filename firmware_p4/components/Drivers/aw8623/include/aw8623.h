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

#ifndef AW8623_H
#define AW8623_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "board_control.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t chip_id;
  uint8_t system_status;
  uint8_t interrupts;
  bool is_identified;
  bool is_reset_confirmed;
  bool has_irq_event;
} aw8623_diagnostic_t;

#define AW8623_NOMINAL_MV      3000
#define AW8623_MAX_DURATION_MS 500
#define AW8623_INTENSITY_MAX   100

typedef struct {
  uint8_t intensity;
  uint16_t duration_ms;
  bool (*is_cancelled)(void);
} aw8623_playback_t;

// Blocking worker API. One finite RAM waveform, no retries of GO.
esp_err_t aw8623_play(const board_control_ops_t *controls, const aw8623_playback_t *request);

// Task-only, serialized and bounded. No playback or calibration is performed.
// Reads SYSINT once (read-to-clear), then asserts remote reset on every exit.
esp_err_t aw8623_probe(const board_control_ops_t *controls, aw8623_diagnostic_t *out_result);
// Hardware stop through RSTN; does not write to an unidentified I2C device.
esp_err_t aw8623_stop(const board_control_ops_t *controls);

#ifdef __cplusplus
}
#endif
#endif

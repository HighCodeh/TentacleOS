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

#ifndef RFID_LAB_IO_H
#define RFID_LAB_IO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RFID_LAB_RAW_CAPACITY 256U
#define RFID_LAB_SAMPLE_BATCH 128U
typedef struct {
  uint32_t duration_us;
  uint8_t level;
} rfid_lab_edge_t;
typedef struct {
  int64_t time_us;
  uint32_t cl_rises;
  uint32_t da_edges;
  uint32_t capture_edges;
  uint32_t dropped;
  int cl_level;
  int da_level;
} rfid_lab_feedback_t;
// All control APIs belong to the single UART/event-loop task.
esp_err_t rfid_lab_io_init(void);
esp_err_t rfid_lab_io_off(void);
esp_err_t rfid_lab_io_pwm(uint32_t pin, uint32_t hz, uint32_t duty, uint32_t *out_hz);
esp_err_t rfid_lab_io_static(uint32_t pin, uint32_t level);
esp_err_t rfid_lab_io_emulate(uint32_t carrier_hz, bool invert);
esp_err_t rfid_lab_io_capture(bool enable);
esp_err_t rfid_lab_io_feedback(rfid_lab_feedback_t *out);
size_t rfid_lab_io_pop(rfid_lab_edge_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif

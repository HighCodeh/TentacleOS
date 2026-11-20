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

#ifndef BOARD_CONTROL_H
#define BOARD_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BOARD_CONTROL_AMP,
  BOARD_CONTROL_HAPTIC,
  BOARD_CONTROL_USB,
  BOARD_CONTROL_COUNT
} board_control_id_t;

typedef struct {
  bool has_request;
  bool requested;
  bool confirmed;
  bool is_known;
  bool is_pending;
  esp_err_t result;
} board_control_output_t;

typedef struct {
  bool is_compatible;
  uint64_t epoch;
  uint32_t capabilities;
  bool is_haptic_int_high;
  bool is_haptic_irq_pending;
  bool is_input_known;
  board_control_output_t outputs[BOARD_CONTROL_COUNT];
  esp_err_t link_result;
} board_control_status_t;

// Requests only enqueue. ESP_OK is acceptance, not hardware confirmation.
// Obtain a fresh snapshot to observe completion; never invoke from ISR.
// Haptic true releases reset; USB true enables /OE (active low).
typedef struct {
  esp_err_t (*request_amplifier)(bool enabled);
  esp_err_t (*request_haptic_release)(bool released);
  esp_err_t (*request_usb_enable)(bool enabled);
  esp_err_t (*get_status)(board_control_status_t *out_status);
} board_control_ops_t;

// Worker-only bounded confirmation; no SPI dependency. Caller serializes its control.
esp_err_t board_control_apply(const board_control_ops_t *ops,
                              board_control_id_t control,
                              bool value,
                              uint64_t *out_epoch);

#ifdef __cplusplus
}
#endif
#endif

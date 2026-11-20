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

#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_pins_init(void);
esp_err_t board_pins_set_amplifier(bool enabled);
esp_err_t board_pins_set_haptic_release(bool released);
esp_err_t board_pins_set_usb_enable(bool enabled);
bool board_pins_haptic_int_high(void);
bool board_pins_haptic_irq_pending(void);

#ifdef __cplusplus
}
#endif

#endif

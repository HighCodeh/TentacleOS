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

#ifndef BOARD_SERVICE_H
#define BOARD_SERVICE_H

#include "board_control.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize once at boot after the SPI master. No driver depends on SPI APIs.
esp_err_t board_service_init(void);
// Recovery owner: discard old intents and negotiate fresh capabilities/state.
esp_err_t board_service_reconcile(void);
const board_control_ops_t *board_service_controls(void);

#ifdef __cplusplus
}
#endif
#endif

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

#ifndef HAPTIC_H
#define HAPTIC_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAPTIC_FEEDBACK_INTENSITY 70
#define HAPTIC_FEEDBACK_MS        80

// Nonblocking requests; no queue or automatic replay. A busy request is rejected.
esp_err_t haptic_play(uint8_t intensity, uint16_t duration_ms);
esp_err_t haptic_probe(void);
void haptic_stop(void);
bool haptic_is_busy(void);
esp_err_t haptic_last_result(void);

#ifdef __cplusplus
}
#endif
#endif

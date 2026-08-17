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

#ifndef C5_LOG_H
#define C5_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// C5 log tee. Hooks esp_log_set_vprintf so every ESP_LOGx line is still printed
// on the local C5 dev console AND copied (ANSI stripped) into a drop-oldest
// ring. A worker forwards each line to the P4 over the SPI_ID_SYSTEM_LOG stream
// as [level u8][utf-8 text]; the P4 relays it to the companion as a LOG frame
// with source=C5.

/** @brief Install the C5 log tee and start the forwarding worker. */
esp_err_t c5_log_init(void);

#ifdef __cplusplus
}
#endif

#endif // C5_LOG_H

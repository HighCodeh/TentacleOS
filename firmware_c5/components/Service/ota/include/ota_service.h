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

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// Receive a firmware push from the P4 over UART0 and flash it, on demand.
//
// The OTA is triggered by the P4 over SPI (SPI_ID_SYSTEM_START_UART_OTA), not by
// a UART magic: UART0 is the console the rest of the time. This call takes UART0
// over from the console, silences logging (so no byte corrupts the raw transfer),
// replies READY, receives the image into the inactive OTA partition with the
// esp_ota APIs, and reboots into the new app on success. On failure it restores
// logging, releases UART0, and returns an error. Call from the SPI bridge task.
esp_err_t ota_service_run_uart(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_SERVICE_H

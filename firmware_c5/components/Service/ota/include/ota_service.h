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

#include <stdint.h>

#include "esp_err.h"

#include "spi_protocol.h"

// App OTA entirely over SPI - no UART involved.
//
// Flow:
//   1. P4 sends SPI_ID_SYSTEM_OTA_BEGIN with the image size -> begin(): a task
//      erases the target partition and sets state=READY (async so the bridge
//      stays responsive during the multi-second erase).
//   2. P4 polls SPI_ID_SYSTEM_OTA_STATUS -> get_status() until READY.
//   3. P4 sends the image as SPI_ID_SYSTEM_OTA_DATA chunks -> write(); each is
//      written sequentially and acked by the command response. bytes_written is
//      also the resync point if a chunk's response is lost.
//   4. When bytes_written reaches the size, write() finalizes, sets DONE, and
//      reboots into the new app.

// Begin an OTA of @p size bytes over @p transport (spi_ota_transport_t): validate,
// spawn the receiver/writer task, return at once. For SPI the image arrives via
// ota_service_write (OTA_DATA commands); for UART the task reads it from UART0.
// Returns ESP_ERR_INVALID_STATE if one is already running.
esp_err_t ota_service_begin(uint32_t size, uint8_t transport);

// Write one firmware chunk (from an OTA_DATA command). Sequential; advances
// bytes_written and finalizes+reboots on the last chunk. Returns the esp_ota
// result so the bridge can ack/nak the chunk over SPI.
esp_err_t ota_service_write(const uint8_t *data, uint16_t len);

// Fill @p out with the current OTA state and byte count. Non-blocking.
void ota_service_get_status(spi_ota_status_t *out);

// Validate a freshly booted OTA image: if the running partition is pending
// verification, wait for the P4 to reach us over the bridge and mark the app
// valid, otherwise roll back. No-op on a normal boot. Call after kernel_init so
// the bridge slave is already listening. Blocks up to the validation window.
esp_err_t ota_post_boot_check(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_SERVICE_H

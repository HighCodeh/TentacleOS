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

#ifndef C5_FLASHER_H
#define C5_FLASHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

// Prepare SPI OTA; no UART GPIOs are configured on V2.03.
esp_err_t c5_flasher_init(void);

// Push a new C5 app image to the running C5 (its app OTA receiver, not ROM
// download). The C5 erases its inactive slot, receives the image, validates it,
// sets it to boot and reboots. A failed transfer is harmless (old slot boots).
//
// The whole control plane (begin, status, per-chunk acks) is on the SPI bridge.
// @p transport (spi_ota_transport_t) selects how the image bytes travel:
//   SPI_OTA_TRANSPORT_SPI  - as SPI_ID_SYSTEM_OTA_DATA chunks (no UART wiring).
//   Other transports return ESP_ERR_NOT_SUPPORTED on V2.03.
// If @p bin_data is NULL the image is streamed from /sdcard/c5/TentacleOS_C5.bin.
esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size, uint8_t transport);

// Ping the C5 over the SPI bridge (SPI_ID_SYSTEM_PING). ESP_OK if it answers.
esp_err_t c5_flasher_ping(void);

// Read and print the C5 chip info over the SPI bridge (SPI_ID_SYSTEM_INFO).
esp_err_t c5_flasher_info(void);

// Re-probe the C5 and re-establish the bridge link (e.g. after the C5 rebooted
// into new firmware). Marks the bridge alive iff the C5 answers a ping.
esp_err_t c5_flasher_sync(void);

// Live OTA progress for the UI progress bar. *sent / *total are bytes; total is
// 0 until streaming begins (during handshake/erase). Safe to call from another
// task while c5_flasher_update() runs.
void c5_flasher_progress(uint32_t *sent, uint32_t *total);

// Ask the running C5 to reboot into ROM serial-download mode over the SPI bridge
// (SPI_ID_SYSTEM_ENTER_DOWNLOAD). After this the C5 is no longer running its app
// (OTA receiver gone); use its separate CP2105 port for external recovery.
// Returns ESP_OK if the C5 acked before rebooting.
esp_err_t c5_flasher_enter_download(void);

// Drain SPI, assert BOOT through T14 and pulse EN through T13; no C5 firmware needed.
// Bridge stays suspended and automatic recovery disabled until P4 restart.
esp_err_t c5_flasher_enter_download_physical(void);

// Ask the running C5 to reboot (SPI_ID_SYSTEM_REBOOT). The C5 acks then restarts;
// the bridge link monitor re-links once it is back. Returns ESP_OK on ack.
esp_err_t c5_flasher_reboot(void);

// Compatibility no-op: the C5 UART is independent of P4.
void c5_flasher_release_uart(void);

// No direct UART path exists on V2.03; returns ESP_ERR_NOT_SUPPORTED.
esp_err_t c5_flasher_rom_flash(void);

// Logs that an external connection to the C5 CP2105 port is required.
void c5_passthrough_run(void);

#ifdef __cplusplus
}
#endif

#endif // C5_FLASHER_H

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

#ifndef C5_LEGACY_H
#define C5_LEGACY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// Minimal client for the legacy InkTest SPI bridge protocol (32-byte frames,
// no IRQ, response delivered on the next transfer). Used only to recover a C5
// that still runs InkTest: the P4 speaks the old protocol to confirm the C5 is
// alive and to command it into ROM download mode, after which 'c5 rom' flashes
// the embedded TentacleOS image over UART.
//
// Each call opens a legacy session (suspends the normal TentacleOS bridge and
// swaps the SPI device to InkTest's 4 MHz config), runs, then restores the
// normal bridge.

/**
 * @brief PING the InkTest C5 (diagnostic).
 *
 * @return ESP_OK if the C5 replied with the expected PONG magic,
 *         ESP_ERR_INVALID_RESPONSE on a bad/absent reply, or a transport error.
 */
esp_err_t c5_legacy_ping(void);

/**
 * @brief GET_INFO from the InkTest C5 and print chip model/revision/MACs/heap.
 *
 * @return ESP_OK on a valid reply, otherwise an error code.
 */
esp_err_t c5_legacy_get_info(void);

/**
 * @brief Command the InkTest C5 into ROM serial-download mode.
 *
 * The C5 ACKs, then reboots with FORCE_DOWNLOAD_BOOT set. After this the bridge
 * goes dark until the C5 is reflashed (run 'c5 rom').
 *
 * @return ESP_OK if the command was acknowledged (or the C5 rebooted before the
 *         ack could be read, which is also success), otherwise an error code.
 */
esp_err_t c5_legacy_enter_download(void);

#ifdef __cplusplus
}
#endif

#endif // C5_LEGACY_H

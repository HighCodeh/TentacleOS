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

#ifndef EVIL_TWIN_H
#define EVIL_TWIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Start an evil twin attack using the default captive portal template.
 *
 * @param ssid  SSID to broadcast for the rogue access point.
 */
void evil_twin_start_attack(const char *ssid);

/**
 * @brief Start an evil twin attack with a custom captive portal template.
 *
 * @param ssid           SSID to broadcast for the rogue access point.
 * @param template_path  Path to the custom HTML template file.
 */
void evil_twin_start_attack_with_template(const char *ssid, const char *template_path);

/**
 * @brief Start an evil twin attack on a background task (non-blocking).
 *
 * Returns immediately; the ~1.5 s Wi-Fi/AP switch and DNS/HTTP bring-up run on a
 * task so the SPI dispatch is not blocked (which would time out the P4's start
 * command and let the session watchdog kill the portal).
 *
 * @param ssid           SSID to broadcast for the rogue access point.
 * @param template_path  Custom portal template path, or NULL for the default.
 */
void evil_twin_start_attack_async(const char *ssid, const char *template_path);

/**
 * @brief Stop the evil twin attack and release resources.
 */
void evil_twin_stop_attack(void);

/**
 * @brief Reset the captured password state.
 */
void evil_twin_reset_capture(void);

/**
 * @brief Check if a password has been captured.
 *
 * @return true if a password is available, false otherwise.
 */
bool evil_twin_has_password(void);

/**
 * @brief Get the last captured password.
 *
 * @param[out] out  Buffer to store the password string.
 * @param len       Size of the output buffer.
 */
void evil_twin_get_last_password(char *out, size_t len);

/**
 * @brief Begin receiving a captive portal template over chunked transfer.
 *
 * @param total_size  Total size of the template in bytes.
 */
void evil_twin_tmpl_begin(uint16_t total_size);

/**
 * @brief Receive a chunk of the captive portal template.
 *
 * @param data  Pointer to the chunk data.
 * @param len   Length of the chunk in bytes.
 */
void evil_twin_tmpl_chunk(const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif // EVIL_TWIN_H

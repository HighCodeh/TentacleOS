// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef EVIL_TWIN_H
#define EVIL_TWIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Start an Evil Twin attack with the given SSID.
 *
 * @param ssid  SSID for the fake AP. Must not be NULL.
 */
void evil_twin_start_attack(const char *ssid);

/**
 * @brief Start an Evil Twin attack with a custom captive portal template.
 *
 * @param ssid           SSID for the fake AP. Must not be NULL.
 * @param template_path  Path to HTML template file, or NULL for default.
 */
void evil_twin_start_attack_with_template(const char *ssid, const char *template_path);

/**
 * @brief Stop the Evil Twin attack.
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
 * @param[out] out_buf  Buffer to store the password string.
 * @param len           Size of the output buffer.
 */
void evil_twin_get_last_password(char *out_buf, size_t len);

/**
 * @brief Upload a custom HTML template to the C5 over SPI.
 *
 * @param path  Path to the template file.
 * @return true on success, false on failure.
 */
bool evil_twin_upload_template(const char *path);

/**
 * @brief Save a captured password to the SD card as JSON.
 *
 * @param password  Password string to save. Must not be NULL or empty.
 */
void evil_twin_save_password(const char *password);

#ifdef __cplusplus
}
#endif

#endif // EVIL_TWIN_H

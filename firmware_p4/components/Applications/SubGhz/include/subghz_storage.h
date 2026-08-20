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

#ifndef SUBGHZ_STORAGE_H
#define SUBGHZ_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "subghz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_STORAGE_NAME_MAX  40
#define SUBGHZ_STORAGE_PROTO_MAX 24

/**
 * @brief Metadata for one saved Sub-GHz capture (parsed from its .sub header).
 */
typedef struct {
  char name[SUBGHZ_STORAGE_NAME_MAX];      /**< @brief File name without the .sub suffix. */
  char protocol[SUBGHZ_STORAGE_PROTO_MAX]; /**< @brief Protocol label ("RAW" for raw captures). */
  uint32_t frequency;                      /**< @brief Center frequency in Hz. */
} subghz_storage_entry_t;

/**
 * @brief Initialize the Sub-GHz storage subsystem.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_storage_init(void);

/**
 * @brief List saved captures on the SD card.
 *
 * @param out          Destination array.
 * @param max_entries  Capacity of the destination array.
 * @return Number of entries written, or -1 on error.
 */
int subghz_storage_list(subghz_storage_entry_t *out, int max_entries);

/**
 * @brief Read a saved capture's raw .sub text.
 *
 * @param name      Capture name (without .sub).
 * @param out_buf   Destination buffer (null-terminated on success).
 * @param out_size  Size of the destination buffer.
 * @return Number of bytes read, or -1 on error.
 */
int subghz_storage_read(const char *name, char *out_buf, size_t out_size);

/**
 * @brief Delete a saved capture.
 *
 * @param name  Capture name (without .sub).
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_storage_delete(const char *name);

/**
 * @brief Save a decoded signal to persistent storage.
 *
 * @param name       User-assigned name for the capture.
 * @param data       Decoded signal data.
 * @param frequency  Center frequency in Hz.
 * @param te         Base time element in microseconds.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t subghz_storage_save_decoded(const char *name,
                                      const subghz_data_t *data,
                                      uint32_t frequency,
                                      uint32_t te);

/**
 * @brief Save raw pulse data to persistent storage.
 *
 * @param name       User-assigned name for the capture.
 * @param pulses     Array of signed pulse durations in microseconds.
 * @param count      Number of elements in the pulses array.
 * @param frequency  Center frequency in Hz.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t
subghz_storage_save_raw(const char *name, const int32_t *pulses, size_t count, uint32_t frequency);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_STORAGE_H

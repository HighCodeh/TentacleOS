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

#ifndef STORAGE_STREAM_H
#define STORAGE_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Opaque handle for a streaming file session. */
typedef struct storage_stream *storage_stream_t;

/**
 * @brief Open a file for streaming read/write.
 *
 * @param path  Absolute file path.
 * @param mode  fopen-style mode string ("r", "w", "a", etc.).
 * @return Stream handle, or NULL on failure.
 */
storage_stream_t storage_stream_open(const char *path, const char *mode);

/**
 * @brief Write data to an open stream.
 *
 * @param stream  Stream handle.
 * @param data    Pointer to data buffer.
 * @param size    Number of bytes to write.
 * @return ESP_OK on success, ESP_FAIL on partial write.
 */
esp_err_t storage_stream_write(storage_stream_t stream, const void *data, size_t size);

/**
 * @brief Read data from an open stream.
 *
 * @param stream              Stream handle.
 * @param buf                 Destination buffer.
 * @param size                Maximum bytes to read.
 * @param[out] out_bytes_read  Actual bytes read (can be NULL).
 * @return ESP_OK on success, ESP_FAIL if no data was read.
 */
esp_err_t
storage_stream_read(storage_stream_t stream, void *buf, size_t size, size_t *out_bytes_read);

/**
 * @brief Flush buffered data to disk.
 *
 * @param stream  Stream handle.
 * @return ESP_OK on success.
 */
esp_err_t storage_stream_flush(storage_stream_t stream);

/**
 * @brief Close the stream and free resources.
 *
 * @param stream  Stream handle. NULL is safe.
 * @return ESP_OK on success, ESP_FAIL if fclose fails.
 */
esp_err_t storage_stream_close(storage_stream_t stream);

/**
 * @brief Check if the stream is currently open.
 *
 * @param stream  Stream handle.
 * @return true if open.
 */
bool storage_stream_is_open(storage_stream_t stream);

/**
 * @brief Get the total number of bytes written to the stream.
 *
 * @param stream  Stream handle.
 * @return Bytes written, or 0 if stream is NULL.
 */
size_t storage_stream_bytes_written(storage_stream_t stream);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_STREAM_H

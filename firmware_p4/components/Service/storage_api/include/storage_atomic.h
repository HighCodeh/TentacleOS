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

#ifndef STORAGE_ATOMIC_H
#define STORAGE_ATOMIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "esp_err.h"

/**
 * @brief Atomically replace a file's contents.
 *
 * Writes @p len bytes to "<path>.tmp", flushes and fsyncs it, then renames it
 * over @p path. On FAT (where rename cannot replace an existing target) it
 * unlinks the target first and retries. The original file is left untouched on
 * any failure before the rename, so a reset never leaves a half-written
 * destination. If power is lost between the unlink and the rename, the complete
 * "<path>.tmp" survives for the loader to recover.
 *
 * Works on both FAT (SD) and LittleFS (flash) mounts.
 *
 * @param path  Destination file path. Must not be NULL.
 * @param data  Buffer to write. May be NULL only when @p len is 0.
 * @param len   Number of bytes to write.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG on bad arguments
 *   - ESP_ERR_INVALID_SIZE if the temp path does not fit
 *   - ESP_FAIL on any I/O failure (destination preserved unless noted above)
 */
esp_err_t storage_write_atomic(const char *path, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_ATOMIC_H

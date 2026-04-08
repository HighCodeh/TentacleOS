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

#ifndef STORAGE_MKDIR_H
#define STORAGE_MKDIR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Create a directory path recursively (like mkdir -p).
 *
 * Creates each component of the path that does not already exist.
 * Existing directories are silently skipped. Fails if any path
 * component exists but is not a directory.
 *
 * @param path  Absolute path to create.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if path is NULL.
 */
esp_err_t storage_mkdir_recursive(const char *path);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_MKDIR_H

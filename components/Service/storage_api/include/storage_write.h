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

/**
 * @file storage_write.h
 * @brief Write Operations - Backend Agnostic
 * @version 1.0
 * 
 * Works with any backend (SD Card, SPIFFS, LittleFS, RAMFS)
 */

#ifndef STORAGE_WRITE_H
#define STORAGE_WRITE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * STRING WRITE
 * ============================================================================ */

esp_err_t storage_write_string(const char *path, const char *data);
esp_err_t storage_append_string(const char *path, const char *data);
esp_err_t storage_write_binary(const char *path, const void *data, size_t size);
esp_err_t storage_append_binary(const char *path, const void *data, size_t size);
esp_err_t storage_write_line(const char *path, const char *line);
esp_err_t storage_append_line(const char *path, const char *line);

/* ============================================================================
 * FORMATTED WRITE
 * ============================================================================ */

esp_err_t storage_write_formatted(const char *path, const char *format, ...) 
    __attribute__((format(printf, 2, 3)));
esp_err_t storage_append_formatted(const char *path, const char *format, ...) 
    __attribute__((format(printf, 2, 3)));

esp_err_t storage_write_buffer(const char *path, const void *buffer, size_t size);
esp_err_t storage_write_bytes(const char *path, const uint8_t *bytes, size_t count);
esp_err_t storage_write_byte(const char *path, uint8_t byte);
esp_err_t storage_write_int(const char *path, int32_t value);
esp_err_t storage_write_float(const char *path, float value);

esp_err_t storage_write_csv_row(const char *path, const char **columns, size_t num_columns);
esp_err_t storage_append_csv_row(const char *path, const char **columns, size_t num_columns);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_WRITE_H
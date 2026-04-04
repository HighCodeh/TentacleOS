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
 * @file storage_read.h
 * @brief Read Operations - Backend Agnostic
 * @version 1.0
 */

#ifndef STORAGE_READ_H
#define STORAGE_READ_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * STRING READ
 * ============================================================================ */

esp_err_t storage_read_string(const char *path, char *buffer, size_t buffer_size);
esp_err_t storage_read_binary(const char *path, void *buffer, size_t size, size_t *bytes_read);


typedef void (*storage_line_callback_t)(const char *line, void *user_data);
esp_err_t storage_read_line(const char *path, char *buffer, size_t buffer_size, uint32_t line_number);
esp_err_t storage_read_first_line(const char *path, char *buffer, size_t buffer_size);
esp_err_t storage_read_last_line(const char *path, char *buffer, size_t buffer_size);
esp_err_t storage_read_lines(const char *path, storage_line_callback_t callback, void *user_data);
esp_err_t storage_count_lines(const char *path, uint32_t *line_count);
esp_err_t storage_read_chunk(const char *path, size_t offset, void *buffer, size_t size, size_t *bytes_read);
esp_err_t storage_read_int(const char *path, int32_t *value);
esp_err_t storage_read_float(const char *path, float *value);
esp_err_t storage_read_bytes(const char *path, uint8_t *bytes, size_t max_count, size_t *count);
esp_err_t storage_read_byte(const char *path, uint8_t *byte);

esp_err_t storage_file_contains(const char *path, const char *search, bool *found);
esp_err_t storage_count_occurrences(const char *path, const char *search, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif
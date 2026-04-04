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



#include "sd_card_write.h"
#include "sd_card_init.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const char *TAG = "sd_write";

#define MAX_PATH_LEN 256

static esp_err_t format_path(const char *path, char *full_path, size_t size)
{
    if (path[0] == '/') {
        snprintf(full_path, size, "%s%s", SD_MOUNT_POINT, path);
    } else {
        snprintf(full_path, size, "%s/%s", SD_MOUNT_POINT, path);
    }
    return ESP_OK;
}

esp_err_t sd_write_string(const char *path, const char *data)
{
    if (!sd_is_mounted() || path == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Escrita incompleta");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Escrito: %s (%d bytes)", full_path, written);
    return ESP_OK;
}

esp_err_t sd_append_string(const char *path, const char *data)
{
    if (!sd_is_mounted() || path == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Anexação incompleta");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Anexado: %s (%d bytes)", full_path, written);
    return ESP_OK;
}

esp_err_t sd_write_binary(const char *path, const void *data, size_t size)
{
    if (!sd_is_mounted() || path == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Escrita binária incompleta");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Binário escrito: %s (%d bytes)", full_path, written);
    return ESP_OK;
}

esp_err_t sd_append_binary(const char *path, const void *data, size_t size)
{
    if (!sd_is_mounted() || path == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "ab");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Anexação binária incompleta");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Binário anexado: %s (%d bytes)", full_path, written);
    return ESP_OK;
}

esp_err_t sd_write_line(const char *path, const char *line)
{
    if (!sd_is_mounted() || path == NULL || line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    fprintf(f, "%s\n", line);
    fclose(f);

    ESP_LOGD(TAG, "Linha escrita: %s", full_path);
    return ESP_OK;
}

esp_err_t sd_append_line(const char *path, const char *line)
{
    if (!sd_is_mounted() || path == NULL || line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    fprintf(f, "%s\n", line);
    fclose(f);

    ESP_LOGD(TAG, "Linha anexada: %s", full_path);
    return ESP_OK;
}

esp_err_t sd_write_formatted(const char *path, const char *format, ...)
{
    if (!sd_is_mounted() || path == NULL || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);

    fclose(f);

    ESP_LOGD(TAG, "Formatado escrito: %s", full_path);
    return ESP_OK;
}

esp_err_t sd_append_formatted(const char *path, const char *format, ...)
{
    if (!sd_is_mounted() || path == NULL || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);

    fclose(f);

    ESP_LOGD(TAG, "Formatado anexado: %s", full_path);
    return ESP_OK;
}

esp_err_t sd_write_buffer(const char *path, const void *buffer, size_t size)
{
    return sd_write_binary(path, buffer, size);
}

esp_err_t sd_write_bytes(const char *path, const uint8_t *bytes, size_t count)
{
    return sd_write_binary(path, bytes, count);
}

esp_err_t sd_write_byte(const char *path, uint8_t byte)
{
    return sd_write_binary(path, &byte, 1);
}

esp_err_t sd_write_int(const char *path, int32_t value)
{
    return sd_write_formatted(path, "%d", value);
}

esp_err_t sd_write_float(const char *path, float value)
{
    return sd_write_formatted(path, "%.6f", value);
}

esp_err_t sd_write_csv_row(const char *path, const char **columns, size_t num_columns)
{
    if (!sd_is_mounted() || path == NULL || columns == NULL || num_columns == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    for (size_t i = 0; i < num_columns; i++) {
        fprintf(f, "%s", columns[i]);
        if (i < num_columns - 1) {
            fprintf(f, ",");
        }
    }
    fprintf(f, "\n");

    fclose(f);

    ESP_LOGD(TAG, "CSV escrito: %s", full_path);
    return ESP_OK;
}

esp_err_t sd_append_csv_row(const char *path, const char **columns, size_t num_columns)
{
    if (!sd_is_mounted() || path == NULL || columns == NULL || num_columns == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    for (size_t i = 0; i < num_columns; i++) {
        fprintf(f, "%s", columns[i]);
        if (i < num_columns - 1) {
            fprintf(f, ",");
        }
    }
    fprintf(f, "\n");

    fclose(f);

    ESP_LOGD(TAG, "CSV anexado: %s", full_path);
    return ESP_OK;
}

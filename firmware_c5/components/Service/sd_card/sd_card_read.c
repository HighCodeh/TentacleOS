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

#include "sd_card_read.h"
#include "sd_card_init.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "sd_read";

#define MAX_PATH_LEN 256
#define MAX_LINE_LEN 512

static esp_err_t format_path(const char *path, char *full_path, size_t size)
{
    if (path[0] == '/') {
        snprintf(full_path, size, "%s%s", SD_MOUNT_POINT, path);
    } else {
        snprintf(full_path, size, "%s/%s", SD_MOUNT_POINT, path);
    }
    return ESP_OK;
}

esp_err_t sd_read_string(const char *path, char *buffer, size_t buffer_size)
{
    if (!sd_is_mounted() || path == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    size_t read_size = fread(buffer, 1, buffer_size - 1, f);
    buffer[read_size] = '\0';
    fclose(f);

    ESP_LOGD(TAG, "Lido: %s (%d bytes)", full_path, read_size);
    return ESP_OK;
}

esp_err_t sd_read_binary(const char *path, void *buffer, size_t size, size_t *bytes_read)
{
    if (!sd_is_mounted() || path == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    size_t read = fread(buffer, 1, size, f);
    fclose(f);

    if (bytes_read != NULL) {
        *bytes_read = read;
    }

    ESP_LOGD(TAG, "Binário lido: %s (%d bytes)", full_path, read);
    return ESP_OK;
}

esp_err_t sd_read_line(const char *path, char *buffer, size_t buffer_size, uint32_t line_number)
{
    if (!sd_is_mounted() || path == NULL || buffer == NULL || line_number == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    uint32_t current_line = 0;
    while (fgets(buffer, buffer_size, f) != NULL) {
        current_line++;
        if (current_line == line_number) {
            char *pos = strchr(buffer, '\n');
            if (pos) *pos = '\0';
            fclose(f);
            ESP_LOGD(TAG, "Linha %lu lida", line_number);
            return ESP_OK;
        }
    }

    fclose(f);
    ESP_LOGW(TAG, "Linha %lu não encontrada", line_number);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sd_read_lines(const char *path, sd_line_callback_t callback, void *user_data)
{
    if (!sd_is_mounted() || path == NULL || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *pos = strchr(line, '\n');
        if (pos) *pos = '\0';
        callback(line, user_data);
    }

    fclose(f);
    ESP_LOGD(TAG, "Todas linhas processadas: %s", full_path);
    return ESP_OK;
}

esp_err_t sd_count_lines(const char *path, uint32_t *line_count)
{
    if (!sd_is_mounted() || path == NULL || line_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    *line_count = 0;
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f) != NULL) {
        (*line_count)++;
    }

    fclose(f);
    ESP_LOGD(TAG, "Contagem: %lu linhas", *line_count);
    return ESP_OK;
}

esp_err_t sd_read_chunk(const char *path, size_t offset, void *buffer, size_t size, size_t *bytes_read)
{
    if (!sd_is_mounted() || path == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    if (fseek(f, offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Erro ao posicionar offset");
        fclose(f);
        return ESP_FAIL;
    }

    size_t read = fread(buffer, 1, size, f);
    fclose(f);

    if (bytes_read != NULL) {
        *bytes_read = read;
    }

    ESP_LOGD(TAG, "Chunk lido: offset=%d, size=%d", offset, read);
    return ESP_OK;
}

esp_err_t sd_read_first_line(const char *path, char *buffer, size_t buffer_size)
{
    return sd_read_line(path, buffer, buffer_size, 1);
}

esp_err_t sd_read_last_line(const char *path, char *buffer, size_t buffer_size)
{
    if (!sd_is_mounted() || path == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    char last_line[MAX_LINE_LEN] = {0};
    char line[MAX_LINE_LEN];
    
    while (fgets(line, sizeof(line), f) != NULL) {
        strncpy(last_line, line, sizeof(last_line) - 1);
    }

    fclose(f);

    if (last_line[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }

    char *pos = strchr(last_line, '\n');
    if (pos) *pos = '\0';

    strncpy(buffer, last_line, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';

    ESP_LOGD(TAG, "Última linha lida");
    return ESP_OK;
}

esp_err_t sd_read_int(const char *path, int32_t *value)
{
    if (!sd_is_mounted() || path == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char buffer[32];
    esp_err_t ret = sd_read_string(path, buffer, sizeof(buffer));
    if (ret == ESP_OK) {
        *value = atoi(buffer);
    }
    return ret;
}

esp_err_t sd_read_float(const char *path, float *value)
{
    if (!sd_is_mounted() || path == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char buffer[32];
    esp_err_t ret = sd_read_string(path, buffer, sizeof(buffer));
    if (ret == ESP_OK) {
        *value = atof(buffer);
    }
    return ret;
}

esp_err_t sd_read_bytes(const char *path, uint8_t *bytes, size_t max_count, size_t *count)
{
    return sd_read_binary(path, bytes, max_count, count);
}

esp_err_t sd_read_byte(const char *path, uint8_t *byte)
{
    if (byte == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t read;
    return sd_read_binary(path, byte, 1, &read);
}

esp_err_t sd_file_contains(const char *path, const char *search, bool *found)
{
    if (!sd_is_mounted() || path == NULL || search == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    *found = false;
    char line[MAX_LINE_LEN];
    
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strstr(line, search) != NULL) {
            *found = true;
            break;
        }
    }

    fclose(f);
    ESP_LOGD(TAG, "Busca por '%s': %s", search, *found ? "encontrado" : "não encontrado");
    return ESP_OK;
}

esp_err_t sd_count_occurrences(const char *path, const char *search, uint32_t *count)
{
    if (!sd_is_mounted() || path == NULL || search == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_path[MAX_PATH_LEN];
    format_path(path, full_path, sizeof(full_path));

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir: %s", full_path);
        return ESP_FAIL;
    }

    *count = 0;
    char line[MAX_LINE_LEN];
    
    while (fgets(line, sizeof(line), f) != NULL) {
        char *pos = line;
        while ((pos = strstr(pos, search)) != NULL) {
            (*count)++;
            pos += strlen(search);
        }
    }

    fclose(f);
    ESP_LOGD(TAG, "Ocorrências de '%s': %lu", search, *count);
    return ESP_OK;
}

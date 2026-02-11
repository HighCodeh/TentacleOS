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

#include "sd_card_file.h"
#include "sd_card_init.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

static const char *TAG = "sd_file";

#define MAX_PATH_LEN      512
#define COPY_BUFFER_SIZE  512

/**
 * @brief Resolve automaticamente o caminho
 */
static esp_err_t format_path(const char *path, char *out, size_t size)
{
    if (!path || !out)
        return ESP_ERR_INVALID_ARG;

    // Já contém o mount point correto
    if (strncmp(path, SD_MOUNT_POINT, strlen(SD_MOUNT_POINT)) == 0)
    {
        snprintf(out, size, "%s", path);
        return ESP_OK;
    }

    // Começa com "/sdcard"
    if (strncmp(path, "/sdcard", 7) == 0)
    {
        snprintf(out, size, "%s", path);
        return ESP_OK;
    }

    // Usuário escreveu "sdcard/..."
    if (strncmp(path, "sdcard/", 7) == 0)
    {
        snprintf(out, size, "/%s", path);
        return ESP_OK;
    }

    // Caminho relativo sem barra
    if (path[0] != '/')
    {
        snprintf(out, size, "%s/%s", SD_MOUNT_POINT, path);
        return ESP_OK;
    }

    // Caminho absoluto sem mount point
    snprintf(out, size, "%s%s", SD_MOUNT_POINT, path);

    return ESP_OK;
}

bool sd_file_exists(const char *path)
{
    if (!sd_is_mounted()) {
        ESP_LOGW(TAG, "SD não está montado");
        return false;
    }

    char full[MAX_PATH_LEN];
    if (format_path(path, full, sizeof(full)) != ESP_OK)
        return false;

    struct stat st;
    if (stat(full, &st) != 0) {
        ESP_LOGD(TAG, "Arquivo não existe: %s", full);
        return false;
    }
    
    return true;
}

esp_err_t sd_file_delete(const char *path)
{
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado");
        return ESP_ERR_INVALID_STATE;
    }

    char full[MAX_PATH_LEN];
    format_path(path, full, sizeof(full));

    if (!sd_file_exists(path)) {
        ESP_LOGE(TAG, "Arquivo não existe: %s", full);
        return ESP_ERR_NOT_FOUND;
    }

    if (unlink(full) != 0)
    {
        ESP_LOGE(TAG, "Erro ao deletar %s: %s", full, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Arquivo deletado: %s", full);
    return ESP_OK;
}

esp_err_t sd_file_rename(const char *old_path, const char *new_path)
{
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado");
        return ESP_ERR_INVALID_STATE;
    }

    char old_full[MAX_PATH_LEN];
    char new_full[MAX_PATH_LEN];

    format_path(old_path, old_full, sizeof(old_full));
    format_path(new_path, new_full, sizeof(new_full));

    if (!sd_file_exists(old_path)) {
        ESP_LOGE(TAG, "Arquivo origem não existe: %s", old_full);
        return ESP_ERR_NOT_FOUND;
    }

    if (rename(old_full, new_full) != 0)
    {
        ESP_LOGE(TAG, "Erro ao renomear %s -> %s: %s", old_full, new_full, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Renomeado: %s -> %s", old_full, new_full);
    return ESP_OK;
}

esp_err_t sd_file_copy(const char *src, const char *dst)
{
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado");
        return ESP_ERR_INVALID_STATE;
    }

    char src_full[MAX_PATH_LEN];
    char dst_full[MAX_PATH_LEN];

    format_path(src, src_full, sizeof(src_full));
    format_path(dst, dst_full, sizeof(dst_full));

    if (!sd_file_exists(src)) {
        ESP_LOGE(TAG, "Arquivo origem não existe: %s", src_full);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *fs = fopen(src_full, "rb");
    if (!fs)
    {
        ESP_LOGE(TAG, "Erro abrindo origem %s: %s", src_full, strerror(errno));
        return ESP_FAIL;
    }

    FILE *fd = fopen(dst_full, "wb");
    if (!fd)
    {
        ESP_LOGE(TAG, "Erro criando destino %s: %s", dst_full, strerror(errno));
        fclose(fs);
        return ESP_FAIL;
    }

    uint8_t *buf = malloc(COPY_BUFFER_SIZE);
    if (!buf)
    {
        fclose(fs);
        fclose(fd);
        return ESP_ERR_NO_MEM;
    }

    size_t read;
    esp_err_t ret = ESP_OK;

    while ((read = fread(buf, 1, COPY_BUFFER_SIZE, fs)) > 0)
    {
        if (fwrite(buf, 1, read, fd) != read)
        {
            ESP_LOGE(TAG, "Erro escrevendo em %s", dst_full);
            ret = ESP_FAIL;
            break;
        }
    }

    free(buf);
    fclose(fs);
    fclose(fd);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Copiado: %s -> %s", src_full, dst_full);
    }

    return ret;
}

esp_err_t sd_file_get_size(const char *path, size_t *size)
{
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado");
        return ESP_ERR_INVALID_STATE;
    }

    if (!size) {
        return ESP_ERR_INVALID_ARG;
    }

    char full[MAX_PATH_LEN];
    format_path(path, full, sizeof(full));

    if (!sd_file_exists(path)) {
        ESP_LOGE(TAG, "Arquivo não existe: %s", full);
        return ESP_ERR_NOT_FOUND;
    }

    struct stat st;
    if (stat(full, &st) != 0) {
        ESP_LOGE(TAG, "Erro ao obter stat de %s: %s", full, strerror(errno));
        return ESP_FAIL;
    }

    *size = st.st_size;
    return ESP_OK;
}

esp_err_t sd_file_get_info(const char *path, sd_file_info_t *info)
{
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado");
        return ESP_ERR_INVALID_STATE;
    }

    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    char full[MAX_PATH_LEN];
    format_path(path, full, sizeof(full));

    if (!sd_file_exists(path)) {
        ESP_LOGE(TAG, "Arquivo não existe: %s", full);
        return ESP_ERR_NOT_FOUND;
    }

    struct stat st;
    if (stat(full, &st) != 0) {
        ESP_LOGE(TAG, "Erro ao obter stat de %s: %s", full, strerror(errno));
        return ESP_FAIL;
    }

    strncpy(info->path, full, sizeof(info->path) - 1);
    info->path[sizeof(info->path) - 1] = '\0';
    info->size = st.st_size;
    info->modified_time = st.st_mtime;
    info->is_directory = S_ISDIR(st.st_mode);

    return ESP_OK;
}

esp_err_t sd_file_truncate(const char *path, size_t size)
{
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado");
        return ESP_ERR_INVALID_STATE;
    }

    char full[MAX_PATH_LEN];
    format_path(path, full, sizeof(full));

    if (!sd_file_exists(path)) {
        ESP_LOGE(TAG, "Arquivo não existe: %s", full);
        return ESP_ERR_NOT_FOUND;
    }

    if (truncate(full, size) != 0) {
        ESP_LOGE(TAG, "Erro ao truncar %s: %s", full, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Truncado: %s para %zu bytes", full, size);
    return ESP_OK;
}

esp_err_t sd_file_is_empty(const char *path, bool *empty)
{
    if (!empty) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t size;
    esp_err_t ret = sd_file_get_size(path, &size);
    if (ret != ESP_OK)
        return ret;

    *empty = (size == 0);
    return ESP_OK;
}

esp_err_t sd_file_move(const char *src, const char *dst)
{
    return sd_file_rename(src, dst);
}

esp_err_t sd_file_compare(const char *path1, const char *path2, bool *equal)
{
    if (!equal) {
        return ESP_ERR_INVALID_ARG;
    }

    *equal = false;

    if (!sd_file_exists(path1) || !sd_file_exists(path2)) {
        ESP_LOGE(TAG, "Um ou ambos arquivos não existem");
        return ESP_ERR_NOT_FOUND;
    }

    size_t s1, s2;
    if (sd_file_get_size(path1, &s1) != ESP_OK ||
        sd_file_get_size(path2, &s2) != ESP_OK)
        return ESP_FAIL;

    if (s1 != s2) {
        return ESP_OK;
    }

    char p1[MAX_PATH_LEN], p2[MAX_PATH_LEN];
    format_path(path1, p1, sizeof(p1));
    format_path(path2, p2, sizeof(p2));

    FILE *f1 = fopen(p1, "rb");
    FILE *f2 = fopen(p2, "rb");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        ESP_LOGE(TAG, "Erro ao abrir arquivos para comparação");
        return ESP_FAIL;
    }

    uint8_t *b1 = malloc(COPY_BUFFER_SIZE);
    uint8_t *b2 = malloc(COPY_BUFFER_SIZE);

    if (!b1 || !b2)
    {
        free(b1); 
        free(b2);
        fclose(f1); 
        fclose(f2);
        return ESP_ERR_NO_MEM;
    }

    bool eq = true;
    size_t r1, r2;

    while ((r1 = fread(b1, 1, COPY_BUFFER_SIZE, f1)) > 0)
    {
        r2 = fread(b2, 1, COPY_BUFFER_SIZE, f2);

        if (r1 != r2 || memcmp(b1, b2, r1) != 0)
        {
            eq = false;
            break;
        }
    }

    free(b1); 
    free(b2);
    fclose(f1); 
    fclose(f2);

    *equal = eq;
    return ESP_OK;
}

esp_err_t sd_file_clear(const char *path)
{
    return sd_file_truncate(path, 0);
}

esp_err_t sd_file_get_extension(const char *path, char *ext, size_t size)
{
    if (!path || !ext || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *dot = strrchr(path, '.');
    if (!dot || dot == path)
    {
        ext[0] = '\0';
        return ESP_OK;
    }

    strncpy(ext, dot + 1, size - 1);
    ext[size - 1] = '\0';

    return ESP_OK;
}

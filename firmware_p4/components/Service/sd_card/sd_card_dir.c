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


#include "sd_card_dir.h"
#include "sd_card_init.h"

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include "esp_log.h"

static const char *TAG = "sd_dir";
#define PATH_BUF 1024

static bool _is_dir(const char *p)
{
    struct stat st;
    return (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
}

esp_err_t sd_dir_create(const char *path)
{
    if (!sd_is_mounted()) return ESP_ERR_INVALID_STATE;

    char full[PATH_BUF];
    snprintf(full, sizeof(full), "%s%s", SD_BASE_PATH, path);

    if (mkdir(full, 0777) == 0) return ESP_OK;
    if (errno == EEXIST) return ESP_OK;

    ESP_LOGE(TAG, "mkdir failed: %s (erro: %s)", full, strerror(errno));
    return ESP_FAIL;
}

static esp_err_t _remove_internal(const char *cur)
{
    DIR *dir = opendir(cur);
    if (!dir) {
        ESP_LOGE(TAG, "Falha ao abrir dir para remover: %s", cur);
        return ESP_FAIL;
    }

    struct dirent *e;

    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        char *child = malloc(PATH_BUF);
        if (!child) {
            closedir(dir);
            return ESP_ERR_NO_MEM;
        }
        
        snprintf(child, PATH_BUF, "%s/%s", cur, e->d_name);

        if (e->d_type == DT_DIR)
        {
            _remove_internal(child);
            if (rmdir(child) != 0) {
                ESP_LOGW(TAG, "Falha ao remover dir: %s", child);
            }
        }
        else
        {
            if (unlink(child) != 0) {
                ESP_LOGW(TAG, "Falha ao remover arquivo: %s", child);
            }
        }

        free(child);
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t sd_dir_remove_recursive(const char *path)
{
    if (!sd_is_mounted()) return ESP_ERR_INVALID_STATE;

    char full[PATH_BUF];
    snprintf(full, sizeof(full), "%s%s", SD_BASE_PATH, path);

    esp_err_t ret = _remove_internal(full);
    
    if (ret == ESP_OK) {
        if (rmdir(full) != 0) {
            ESP_LOGE(TAG, "Falha ao remover diretório raiz: %s", full);
            return ESP_FAIL;
        }
    }
    
    return ret;
}

bool sd_dir_exists(const char *path)
{
    char full[PATH_BUF];
    snprintf(full, sizeof(full), "%s%s", SD_BASE_PATH, path);
    return _is_dir(full);
}

esp_err_t sd_dir_list(const char *path, sd_dir_callback_t cb, void *user_data)
{
    char full[PATH_BUF];
    snprintf(full, PATH_BUF, "%s%s", SD_BASE_PATH, path);

    DIR *dir = opendir(full);
    if (!dir) {
        ESP_LOGE(TAG, "Falha ao abrir dir: %s", full);
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *e;

    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        bool is_dir = (e->d_type == DT_DIR);
        cb(e->d_name, is_dir, user_data);
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t sd_dir_count(const char *path, uint32_t *file_count, uint32_t *dir_count)
{
    char full[PATH_BUF];
    snprintf(full, PATH_BUF, "%s%s", SD_BASE_PATH, path);

    DIR *dir = opendir(full);
    if (!dir) {
        ESP_LOGE(TAG, "Falha ao abrir dir: %s", full);
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *e;
    uint32_t f = 0, d = 0;

    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        if (e->d_type == DT_DIR)
            d++;
        else
            f++;
    }

    closedir(dir);

    *file_count = f;
    *dir_count  = d;
    return ESP_OK;
}

static esp_err_t _copy_internal(const char *src, const char *dst)
{
    DIR *dir = opendir(src);
    if (!dir) return ESP_FAIL;

    struct dirent *e;

    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        char *src_child = malloc(PATH_BUF);
        char *dst_child = malloc(PATH_BUF);

        if (!src_child || !dst_child) {
            free(src_child);
            free(dst_child);
            closedir(dir);
            return ESP_ERR_NO_MEM;
        }

        snprintf(src_child, PATH_BUF, "%s/%s", src, e->d_name);
        snprintf(dst_child, PATH_BUF, "%s/%s", dst, e->d_name);

        if (e->d_type == DT_DIR)
        {
            mkdir(dst_child, 0777);
            _copy_internal(src_child, dst_child);
        }
        else
        {
            FILE *fs = fopen(src_child, "rb");
            FILE *fd = fopen(dst_child, "wb");

            if (fs && fd)
            {
                uint8_t buf[512];
                size_t r;
                while ((r = fread(buf, 1, sizeof(buf), fs)) > 0)
                    fwrite(buf, 1, r, fd);
            }

            if (fs) fclose(fs);
            if (fd) fclose(fd);
        }

        free(src_child);
        free(dst_child);
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t sd_dir_copy_recursive(const char *src, const char *dst)
{
    char full_src[PATH_BUF], full_dst[PATH_BUF];

    snprintf(full_src, PATH_BUF, "%s%s", SD_BASE_PATH, src);
    snprintf(full_dst, PATH_BUF, "%s%s", SD_BASE_PATH, dst);

    // Evita recursão infinita - verifica se destino está DENTRO da origem
    size_t src_len = strlen(full_src);
    if (strncmp(full_dst, full_src, src_len) == 0 && 
        (full_dst[src_len] == '/' || full_dst[src_len] == '\0')) {
        ESP_LOGE(TAG, "Destino (%s) está dentro da origem (%s)!", full_dst, full_src);
        return ESP_ERR_INVALID_ARG;
    }

    mkdir(full_dst, 0777);

    return _copy_internal(full_src, full_dst);
}

static uint64_t _size_internal(const char *cur)
{
    DIR *dir = opendir(cur);
    if (!dir) return 0;

    struct dirent *e;
    uint64_t t = 0;

    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        char *child = malloc(PATH_BUF);
        if (!child) {
            closedir(dir);
            return t;
        }
        
        snprintf(child, PATH_BUF, "%s/%s", cur, e->d_name);

        if (e->d_type == DT_DIR)
        {
            t += _size_internal(child);
        }
        else
        {
            struct stat st;
            if (stat(child, &st) == 0)
                t += st.st_size;
        }

        free(child);
    }

    closedir(dir);
    return t;
}

esp_err_t sd_dir_get_size(const char *path, uint64_t *total_size)
{
    char full[PATH_BUF];
    snprintf(full, PATH_BUF, "%s%s", SD_BASE_PATH, path);

    *total_size = _size_internal(full);
    return ESP_OK;
}

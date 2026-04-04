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

#include "sd_card_info.h"
#include "sd_card_init.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include <string.h>

static const char *TAG = "sd_info";

extern sdmmc_card_t* sd_get_card_handle(void);

esp_err_t sd_get_card_info(sd_card_info_t *info)
{
    if (!sd_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    sdmmc_card_t *card = sd_get_card_handle();
    if (card == NULL || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(info->name, card->cid.name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    
    info->capacity_mb = ((uint64_t)card->csd.capacity * card->csd.sector_size) / (1024 * 1024);
    info->sector_size = card->csd.sector_size;
    info->num_sectors = card->csd.capacity;
    info->speed_khz = card->max_freq_khz;
    info->card_type = card->ocr;
    info->is_mounted = sd_is_mounted();

    return ESP_OK;
}

void sd_print_card_info(void)
{
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "SD não montado");
        return;
    }

    sd_card_info_t info;
    if (sd_get_card_info(&info) == ESP_OK) {
        ESP_LOGI(TAG, "========== Info SD ==========");
        ESP_LOGI(TAG, "Nome: %s", info.name);
        ESP_LOGI(TAG, "Capacidade: %lu MB", info.capacity_mb);
        ESP_LOGI(TAG, "Tamanho setor: %lu bytes", info.sector_size);
        ESP_LOGI(TAG, "Num setores: %lu", info.num_sectors);
        ESP_LOGI(TAG, "Velocidade: %lu kHz", info.speed_khz);
        ESP_LOGI(TAG, "Status: %s", info.is_mounted ? "Montado" : "Desmontado");
        ESP_LOGI(TAG, "============================");
    }

    sdmmc_card_t *card = sd_get_card_handle();
    if (card) {
        sdmmc_card_print_info(stdout, card);
    }
}

esp_err_t sd_get_fs_stats(sd_fs_stats_t *stats)
{
    if (!sd_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FATFS *fs;
    DWORD fre_clust;
    
    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) {
        ESP_LOGE(TAG, "Erro ao obter estatísticas do filesystem");
        return ESP_FAIL;
    }

    uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors = fre_clust * fs->csize;

    stats->total_bytes = total_sectors * fs->ssize;
    stats->free_bytes = free_sectors * fs->ssize;
    stats->used_bytes = stats->total_bytes - stats->free_bytes;

    return ESP_OK;
}

esp_err_t sd_get_free_space(uint64_t *free_bytes)
{
    if (free_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_fs_stats_t stats;
    esp_err_t ret = sd_get_fs_stats(&stats);
    if (ret == ESP_OK) {
        *free_bytes = stats.free_bytes;
    }
    return ret;
}

esp_err_t sd_get_total_space(uint64_t *total_bytes)
{
    if (total_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_fs_stats_t stats;
    esp_err_t ret = sd_get_fs_stats(&stats);
    if (ret == ESP_OK) {
        *total_bytes = stats.total_bytes;
    }
    return ret;
}

esp_err_t sd_get_used_space(uint64_t *used_bytes)
{
    if (used_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_fs_stats_t stats;
    esp_err_t ret = sd_get_fs_stats(&stats);
    if (ret == ESP_OK) {
        *used_bytes = stats.used_bytes;
    }
    return ret;
}

esp_err_t sd_get_usage_percent(float *percentage)
{
    if (percentage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_fs_stats_t stats;
    esp_err_t ret = sd_get_fs_stats(&stats);
    if (ret == ESP_OK) {
        if (stats.total_bytes > 0) {
            *percentage = ((float)stats.used_bytes / stats.total_bytes) * 100.0f;
        } else {
            *percentage = 0.0f;
        }
    }
    return ret;
}

esp_err_t sd_get_card_name(char *name, size_t size)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_card_info_t info;
    esp_err_t ret = sd_get_card_info(&info);
    if (ret == ESP_OK) {
        strncpy(name, info.name, size - 1);
        name[size - 1] = '\0';
    }
    return ret;
}

esp_err_t sd_get_capacity(uint32_t *capacity_mb)
{
    if (capacity_mb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_card_info_t info;
    esp_err_t ret = sd_get_card_info(&info);
    if (ret == ESP_OK) {
        *capacity_mb = info.capacity_mb;
    }
    return ret;
}

esp_err_t sd_get_speed(uint32_t *speed_khz)
{
    if (speed_khz == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_card_info_t info;
    esp_err_t ret = sd_get_card_info(&info);
    if (ret == ESP_OK) {
        *speed_khz = info.speed_khz;
    }
    return ret;
}

esp_err_t sd_get_card_type(uint8_t *type)
{
    if (type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sd_card_info_t info;
    esp_err_t ret = sd_get_card_info(&info);
    if (ret == ESP_OK) {
        *type = info.card_type;
    }
    return ret;
}

esp_err_t sd_get_card_type_name(char *type_name, size_t size)
{
    if (type_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t type;
    esp_err_t ret = sd_get_card_type(&type);
    if (ret == ESP_OK) {
        // Simplificado - você pode expandir isso
        snprintf(type_name, size, "SD Type %d", type);
    }
    return ret;
}

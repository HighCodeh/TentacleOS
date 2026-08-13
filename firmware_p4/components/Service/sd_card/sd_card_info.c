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

#include "sd_card_info.h"
#include "sd_card_init.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include <string.h>

static const char *TAG = "SD_CARD_INFO";

extern sdmmc_card_t *sd_get_card_handle(void);

esp_err_t sd_get_card_info(sd_card_info_t *info) {
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

void sd_print_card_info(void) {
  if (!sd_is_mounted()) {
    ESP_LOGE(TAG, "SD not mounted");
    return;
  }

  sd_card_info_t info;
  if (sd_get_card_info(&info) == ESP_OK) {
    ESP_LOGI(TAG, "========== SD Info ==========");
    ESP_LOGI(TAG, "Name: %s", info.name);
    ESP_LOGI(TAG, "Capacity: %lu MB", info.capacity_mb);
    ESP_LOGI(TAG, "Sector size: %lu bytes", info.sector_size);
    ESP_LOGI(TAG, "Sector count: %lu", info.num_sectors);
    ESP_LOGI(TAG, "Speed: %lu kHz", info.speed_khz);
    ESP_LOGI(TAG, "Status: %s", info.is_mounted ? "Mounted" : "Unmounted");
    ESP_LOGI(TAG, "============================");
  }

  sdmmc_card_t *card = sd_get_card_handle();
  if (card) {
    sdmmc_card_print_info(stdout, card);
  }
}

esp_err_t sd_get_fs_stats(sd_fs_stats_t *stats) {
  if (!sd_is_mounted()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (stats == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  FATFS *fs;
  DWORD fre_clust;

  if (f_getfree("0:", &fre_clust, &fs) != FR_OK) {
    ESP_LOGE(TAG, "Failed to get filesystem stats");
    return ESP_FAIL;
  }

  uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
  uint64_t free_sectors = fre_clust * fs->csize;

  stats->total_bytes = total_sectors * FF_MAX_SS;
  stats->free_bytes = free_sectors * FF_MAX_SS;
  stats->used_bytes = stats->total_bytes - stats->free_bytes;

  return ESP_OK;
}

esp_err_t sd_get_free_space(uint64_t *free_bytes) {
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

esp_err_t sd_get_total_space(uint64_t *total_bytes) {
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

esp_err_t sd_get_used_space(uint64_t *used_bytes) {
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

esp_err_t sd_get_usage_percent(float *percentage) {
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

esp_err_t sd_get_card_name(char *name, size_t size) {
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

esp_err_t sd_get_capacity(uint32_t *capacity_mb) {
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

esp_err_t sd_get_speed(uint32_t *speed_khz) {
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

esp_err_t sd_get_card_type(uint8_t *type) {
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

esp_err_t sd_get_card_type_name(char *type_name, size_t size) {
  if (type_name == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t type;
  esp_err_t ret = sd_get_card_type(&type);
  if (ret == ESP_OK) {
    snprintf(type_name, size, "SD Type %d", type);
  }
  return ret;
}

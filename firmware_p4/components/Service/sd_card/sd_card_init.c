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


#include "sd_card_init.h"
#include "pin_def.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_init";
static sdmmc_card_t *s_card = NULL;
static bool s_is_mounted = false;

esp_err_t sd_init(void)
{
    return sd_init_custom(SD_MAX_FILES, false);
}

esp_err_t sd_init_custom(uint8_t max_files, bool format_if_failed)
{
    if (s_is_mounted) {
        ESP_LOGW(TAG, "SD already mounted");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD (SDMMC 4-bit)...");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = SDMMC_CLK_PIN;
    slot_config.cmd = SDMMC_CMD_PIN;
    slot_config.d0 = SDMMC_D0_PIN;
    slot_config.d1 = SDMMC_D1_PIN;
    slot_config.d2 = SDMMC_D2_PIN;
    slot_config.d3 = SDMMC_D3_PIN;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_failed,
        .max_files = max_files,
        .allocation_unit_size = SD_ALLOCATION_UNIT,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &s_card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_is_mounted = true;
    ESP_LOGI(TAG, "SD mounted (SDMMC): %s, %llu MB",
             s_card->cid.name,
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));
    return ESP_OK;
}

esp_err_t sd_deinit(void)
{
    if (!s_is_mounted) {
        ESP_LOGW(TAG, "SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unmount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_is_mounted = false;
    s_card = NULL;

    ESP_LOGI(TAG, "SD unmounted");
    return ESP_OK;
}

bool sd_is_mounted(void)
{
    return s_is_mounted;
}

esp_err_t sd_remount(void)
{
    if (s_is_mounted) {
        esp_err_t ret = sd_deinit();
        if (ret != ESP_OK) return ret;
    }
    return sd_init();
}

esp_err_t sd_check_health(void)
{
    if (!s_is_mounted) {
        ESP_LOGE(TAG, "SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_card == NULL) {
        ESP_LOGE(TAG, "Card pointer is null");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Card healthy");
    return ESP_OK;
}

sdmmc_card_t* sd_get_card_handle(void)
{
    return s_card;
}

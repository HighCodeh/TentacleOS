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
#include "spi.h"
#include "pin_def.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
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
        ESP_LOGW(TAG, "SD já montado");
        return ESP_OK;
    }

    spi_device_config_t sd_cfg = {
        .cs_pin = SD_CARD_CS_PIN,
        .clock_speed_hz = SDMMC_FREQ_DEFAULT * 1000,
        .mode = 0,
        .queue_size = 4,
    };
    
    esp_err_t ret = spi_add_device(SPI_DEVICE_SD_CARD, &sd_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar SD no SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_failed,
        .max_files = max_files,
        .allocation_unit_size = SD_ALLOCATION_UNIT
    };
    
    ESP_LOGI(TAG, "Inicializando SD...");
    
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.slot = SPI3_HOST;
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CARD_CS_PIN;
    slot_config.host_id = host.slot;
    
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, 
                                   &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro mount: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_is_mounted = true;
    ESP_LOGI(TAG, "SD montado com sucesso!");
    return ESP_OK;
}

esp_err_t sd_init_custom_pins(int mosi, int miso, int clk, int cs)
{
    ESP_LOGW(TAG, "Pinos customizados não suportados com driver SPI centralizado");
    ESP_LOGW(TAG, "Usando pinos padrão definidos em pin_def.h");
    return sd_init();
}

esp_err_t sd_deinit(void)
{
    if (!s_is_mounted) {
        ESP_LOGW(TAG, "SD não está montado");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro unmount: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_is_mounted = false;
    s_card = NULL;
    
    ESP_LOGI(TAG, "SD desmontado");
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

esp_err_t sd_reset_bus(void)
{
    ESP_LOGW(TAG, "Reset de barramento não suportado com driver SPI compartilhado");
    ESP_LOGI(TAG, "Use sd_remount() para remontar o cartão");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sd_check_health(void)
{
    if (!s_is_mounted) {
        ESP_LOGE(TAG, "SD não montado");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_card == NULL) {
        ESP_LOGE(TAG, "Ponteiro do cartão nulo");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Cartão saudável");
    return ESP_OK;
}

sdmmc_card_t* sd_get_card_handle(void)
{
    return s_card;
}

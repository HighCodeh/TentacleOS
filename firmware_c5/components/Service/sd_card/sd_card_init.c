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

#include "sd_card_init.h"

#include "esp_log.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SD_CARD_INIT";

esp_err_t sd_init(void) {
  ESP_LOGW(TAG, "V2.03 SD card belongs to P4");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sd_init_custom(uint8_t max_files, bool format_if_failed) {
  (void)max_files;
  (void)format_if_failed;
  return sd_init();
}

esp_err_t sd_init_custom_pins(int mosi, int miso, int clk, int cs) {
  (void)mosi;
  (void)miso;
  (void)clk;
  (void)cs;
  return sd_init();
}

esp_err_t sd_deinit(void) {
  return ESP_ERR_NOT_SUPPORTED;
}
bool sd_is_mounted(void) {
  return false;
}
esp_err_t sd_remount(void) {
  return sd_init();
}
esp_err_t sd_reset_bus(void) {
  return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t sd_check_health(void) {
  return ESP_ERR_NOT_SUPPORTED;
}
sdmmc_card_t *sd_get_card_handle(void) {
  return NULL;
}

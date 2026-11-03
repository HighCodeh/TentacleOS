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

#include "vfs_sdcard.h"

#include "esp_log.h"

static const char *TAG = "VFS_SDCARD";

esp_err_t vfs_sdcard_init(void) {
  ESP_LOGW(TAG, "V2.03 SD card belongs to P4");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t vfs_sdcard_deinit(void) {
  return ESP_ERR_NOT_SUPPORTED;
}
bool vfs_sdcard_is_mounted(void) {
  return false;
}
esp_err_t vfs_register_sd_backend(void) {
  return vfs_sdcard_init();
}
esp_err_t vfs_unregister_sd_backend(void) {
  return ESP_ERR_NOT_SUPPORTED;
}
void vfs_sdcard_print_info(void) {
  ESP_LOGI(TAG, "No local SD card on C5");
}
esp_err_t vfs_sdcard_format(void) {
  return ESP_ERR_NOT_SUPPORTED;
}

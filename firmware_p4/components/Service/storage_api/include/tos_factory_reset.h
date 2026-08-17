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

#ifndef TOS_FACTORY_RESET_H
#define TOS_FACTORY_RESET_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wipe user configuration back to factory defaults.
 *
 * Deletes every config file under the config directories on both storages
 * (the assets LittleFS partition and the SD card, when mounted) and removes the
 * first-boot marker so tos_first_boot_setup re-seeds defaults on the next boot.
 * Directory skeletons are preserved so config saves keep working immediately.
 *
 * Does NOT touch NVS, user captures/loot, themes or shipped assets. The caller
 * is expected to reboot afterwards.
 *
 * @return ESP_OK if all reachable targets were cleared.
 */
esp_err_t tos_factory_reset_config(void);

/**
 * @brief Full factory reset: config + user data + NVS.
 *
 * Runs tos_factory_reset_config, then deletes user captures/loot/themes/scripts
 * on both storages and erases NVS. Never formats the assets partition (shipped
 * icons/html/fonts survive) and never touches the on-SD C5 firmware image, so
 * the device and its C5-recovery path stay bootable. The caller is expected to
 * reboot afterwards.
 *
 * @return ESP_OK if all reachable targets were cleared.
 */
esp_err_t tos_factory_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif // TOS_FACTORY_RESET_H

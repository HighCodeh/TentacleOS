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

#ifndef CONSOLE_SERVICE_H
#define CONSOLE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Initialize the console service and register all commands.
 *
 * @return ESP_OK on success.
 */
esp_err_t console_service_init(void);

/**
 * @brief Start the console REPL task.
 */
void console_service_start(void);

/**
 * @brief Stop and delete the console REPL (frees the console UART).
 */
void console_service_stop(void);

/**
 * @brief Register filesystem commands (ls, cd, pwd, cat).
 */
void register_fs_commands(void);

/**
 * @brief Register system commands (free, restart, tasks, ip).
 */
void register_system_commands(void);

/**
 * @brief Register Wi-Fi commands.
 */
void register_wifi_commands(void);

/**
 * @brief Register BadUSB commands (run, type, layout, stop, status).
 */
void register_badusb_commands(void);

/**
 * @brief Register host-link commands (psk, regen, status).
 */
void register_hostlink_commands(void);

/**
 * @brief Register screen capture/navigation commands (goto, screenshot, key).
 */
void register_screen_commands(void);

/**
 * @brief Register infrared commands (rx, send, deinit).
 */
void register_ir_commands(void);

/**
 * @brief Register battery/charger status command (battery).
 */
void register_battery_commands(void);

/**
 * @brief Register I2C bus commands (i2cscan).
 */
void register_i2c_commands(void);

/**
 * @brief Register RGB status-LED commands (led).
 */
void register_led_commands(void);

/**
 * @brief Register audio commands (audio: volume/tone/chime/click).
 */
void register_audio_commands(void);

/**
 * @brief Register Sub-GHz commands (subghz: rx/tx/spectrum/list/replay).
 */
void register_subghz_commands(void);

/**
 * @brief Register the app loader commands (apprun, appstop, apps, appgrant).
 */
void register_apprun_commands(void);

/**
 * @brief Register the resource arbitration command (resources).
 */
void register_resource_commands(void);

/**
 * @brief Register SD card commands (sd: status, toexfat).
 */
void register_sd_commands(void);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_SERVICE_H

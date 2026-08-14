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

#ifndef KERNEL_H
#define KERNEL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all system peripherals and services.
 *
 * Boot sequence:
 *   1. NVS flash
 *   2. SPI / I2C buses
 *   3. Storage (SD card + assets)
 *   4. Configuration, theme, logging
 *   5. Peripherals (LED, battery, CC1101, bridge)
 *   6. Display + LVGL + UI
 *   7. Wi-Fi, console, system monitor
 *
 * Each subsystem is recorded in the boot map (boot_report). If a required
 * subsystem fails, the boot aborts to safe mode instead of continuing blind.
 *
 * @return ESP_OK on a normal boot, ESP_FAIL when a required subsystem failed
 *         and the device fell back to safe mode.
 */
esp_err_t kernel_init(void);

/**
 * @brief Display a safeguard alert on screen and log the error.
 *
 * Shows a modal message box with a warning icon. Used by the
 * system monitor and FreeRTOS hooks to report critical events.
 *
 * @param title    Alert title (logged via ESP_LOGE).
 * @param message  Alert body displayed in the message box.
 */
void safeguard_alert(const char *title, const char *message);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_H

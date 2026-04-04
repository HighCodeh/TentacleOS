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

#ifndef KERNEL_H
#define KERNEL_H

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
 */
void kernel_init(void);

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

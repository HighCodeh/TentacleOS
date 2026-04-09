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

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_SERVICE_H

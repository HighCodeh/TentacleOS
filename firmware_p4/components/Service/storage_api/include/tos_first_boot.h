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

#ifndef TOS_FIRST_BOOT_H
#define TOS_FIRST_BOOT_H

#include "esp_err.h"

/**
 * @brief Creates the full SD card folder structure on first boot.
 *
 * Checks for a marker file (.setup_done) to avoid re-running.
 * If the marker is absent, creates all protocol folders, sub-folders,
 * system directories, and the config directory. Never overwrites
 * existing data — only creates what is missing.
 *
 * @return ESP_OK on success or if setup was already done
 */
esp_err_t tos_first_boot_setup(void);

#endif // TOS_FIRST_BOOT_H

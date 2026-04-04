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

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

#define OTA_UPDATE_PATH "/sdcard/update/tentacleos.bin"

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_VALIDATING,
    OTA_STATE_WRITING,
    OTA_STATE_REBOOTING,
    OTA_STATE_ERROR,
} ota_state_t;

typedef void (*ota_progress_cb_t)(int percent, const char *message);

bool ota_update_available(void);
esp_err_t ota_start_update(ota_progress_cb_t progress_cb);
esp_err_t ota_post_boot_check(void);
const char* ota_get_current_version(void);
ota_state_t ota_get_state(void);

#endif // OTA_SERVICE_H

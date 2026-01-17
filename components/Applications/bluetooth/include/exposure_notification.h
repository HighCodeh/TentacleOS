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

#ifndef EXPOSURE_NOTIFICATION_H
#define EXPOSURE_NOTIFICATION_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t addr[6];
    int8_t rssi;
    uint32_t last_seen; // Timestamp in seconds
} exposure_device_t;

esp_err_t exposure_notification_start(void);
void exposure_notification_stop(void);
exposure_device_t* exposure_notification_get_list(uint16_t *count);
uint16_t exposure_notification_get_count(void);
void exposure_notification_reset(void);

#endif // EXPOSURE_NOTIFICATION_H

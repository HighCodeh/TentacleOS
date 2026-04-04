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


#ifndef BLE_TRACKER_H
#define BLE_TRACKER_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t ble_tracker_start(const uint8_t *addr);
void ble_tracker_stop(void);
int ble_tracker_get_rssi(void);

#endif // BLE_TRACKER_H

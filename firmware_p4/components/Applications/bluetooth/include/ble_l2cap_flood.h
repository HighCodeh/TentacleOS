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

#ifndef BLE_L2CAP_FLOOD_H
#define BLE_L2CAP_FLOOD_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t ble_l2cap_flood_start(const uint8_t *addr, uint8_t addr_type);
esp_err_t ble_l2cap_flood_stop(void);
bool ble_l2cap_flood_is_running(void);

#endif // BLE_L2CAP_FLOOD_H

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

#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <stdbool.h>
#include <stdint.h>
#include "bluetooth_service.h"

bool ble_scanner_save_results_to_internal_flash(void);
bool ble_scanner_save_results_to_sd_card(void);
bool ble_scanner_start(void);
ble_scan_result_t* ble_scanner_get_results(uint16_t *count);
void ble_scanner_free_results(void);

#endif // BLE_SCANNER_H

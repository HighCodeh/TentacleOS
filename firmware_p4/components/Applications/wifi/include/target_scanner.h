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

#ifndef TARGET_SCANNER_H
#define TARGET_SCANNER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint8_t client_mac[6];
  int8_t rssi;
} target_client_record_t;
bool target_scanner_start(const uint8_t *target_bssid, uint8_t channel);
target_client_record_t* target_scanner_get_results(uint16_t *count);
target_client_record_t* target_scanner_get_live_results(uint16_t *count, bool *scanning);
void target_scanner_free_results(void);
bool target_scanner_save_results_to_internal_flash(void);
bool target_scanner_save_results_to_sd_card(void);

#endif // TARGET_SCANNER_H

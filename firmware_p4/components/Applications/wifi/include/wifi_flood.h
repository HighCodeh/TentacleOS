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

#ifndef WIFI_FLOOD_H
#define WIFI_FLOOD_H

#include <stdbool.h>
#include <stdint.h>

bool wifi_flood_auth_start(const uint8_t *target_bssid, uint8_t channel);
bool wifi_flood_assoc_start(const uint8_t *target_bssid, uint8_t channel);
bool wifi_flood_probe_start(const uint8_t *target_bssid, uint8_t channel);

void wifi_flood_stop(void);
bool wifi_flood_is_running(void);

#endif

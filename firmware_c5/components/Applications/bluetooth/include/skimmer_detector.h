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

#ifndef SKIMMER_DETECTOR_H
#define SKIMMER_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_hs.h"

#define MAX_SKIMMERS_FOUND 50

typedef struct {
    ble_addr_t addr;
    int8_t rssi;
    char name[32];
    uint32_t last_seen;
    char detection_reason[32];
} skimmer_record_t;

bool skimmer_detector_start(void);
void skimmer_detector_stop(void);
skimmer_record_t* skimmer_detector_get_results(uint16_t *count);
void skimmer_detector_clear_results(void);

#endif // SKIMMER_DETECTOR_H
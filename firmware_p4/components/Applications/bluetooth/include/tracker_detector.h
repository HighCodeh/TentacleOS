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

#ifndef TRACKER_DETECTOR_H
#define TRACKER_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_TRACKERS_FOUND 50

typedef enum {
    TRACKER_TYPE_UNKNOWN = 0,
    TRACKER_TYPE_AIRTAG,
    TRACKER_TYPE_SMARTTAG,
    TRACKER_TYPE_TILE,
    TRACKER_TYPE_CHIPOLO
} tracker_type_t;

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int8_t rssi;
    tracker_type_t type;
    char type_str[16];
    uint32_t last_seen;
    uint8_t payload_sample[10];
} tracker_record_t;

bool tracker_detector_start(void);
void tracker_detector_stop(void);
tracker_record_t* tracker_detector_get_results(uint16_t *count);
void tracker_detector_clear_results(void);

#endif // TRACKER_DETECTOR_H

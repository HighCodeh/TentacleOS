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

#ifndef PROBE_MONITOR_H
#define PROBE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint8_t mac[6];
  int8_t rssi;
  char ssid[33];
  uint32_t last_seen_timestamp;
} probe_record_t;

bool probe_monitor_start(void);
void probe_monitor_stop(void);
probe_record_t* probe_monitor_get_results(uint16_t *count);
const uint16_t* probe_monitor_get_count_ptr(void);
void probe_monitor_free_results(void);
bool probe_monitor_save_results_to_internal_flash(void);
bool probe_monitor_save_results_to_sd_card(void);

#endif // PROBE_MONITOR_H

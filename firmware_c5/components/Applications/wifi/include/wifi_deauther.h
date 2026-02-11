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

#ifndef WIFI_DEAUTHER_H
#define WIFI_DEAUTHER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_wifi_types.h"

typedef enum {
  DEAUTH_INVALID_AUTH = 0,
  DEAUTH_INACTIVITY,
  DEAUTH_CLASS3,
  DEAUTH_TYPE_COUNT
} deauth_frame_type_t;

void wifi_deauther_send_deauth_frame(const wifi_ap_record_t *ap_record, deauth_frame_type_t type);
void wifi_deauther_send_broadcast_deauth(const wifi_ap_record_t *ap_record, deauth_frame_type_t type);
void wifi_deauther_send_raw_frame(const uint8_t *frame_buffer, int size);
void wifi_send_association_request(const wifi_ap_record_t *ap_record);

bool wifi_deauther_start(const wifi_ap_record_t *ap_record, deauth_frame_type_t type, bool is_broadcast);
void wifi_deauther_stop(void);
bool wifi_deauther_is_running(void);
#endif // !WIFI_DEAUTHER_H

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

#include "subghz_storage.h"
#include "subghz_protocol_serializer.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = "SUBGHZ_STORAGE";

void subghz_storage_init(void) {
    ESP_LOGI(TAG, "Storage API Initialized (Placeholder Mode)");
}

void subghz_storage_save_decoded(const char* name, const subghz_data_t* data, uint32_t frequency, uint32_t te) {
    char* buf = malloc(1024);
    if (!buf) return;

    subghz_protocol_serialize_decoded(data, frequency, te, buf, 1024);

    ESP_LOGI(TAG, "Saving Decoded Signal to %s...", name);
    printf("\n--- FILE CONTENT START (%s) ---\n", name);
    printf("%s", buf);
    printf("--- FILE CONTENT END ---\n\n");

    free(buf);
}

void subghz_storage_save_raw(const char* name, const int32_t* pulses, size_t count, uint32_t frequency) {
    // RAW signals can be large, allocate 4KB
    char* buf = malloc(4096);
    if (!buf) return;

    subghz_protocol_serialize_raw(pulses, count, frequency, buf, 4096);

    ESP_LOGI(TAG, "Saving RAW Signal to %s...", name);
    printf("\n--- FILE CONTENT START (%s) ---\n", name);
    printf("%s", buf);
    printf("--- FILE CONTENT END ---\n\n");

    free(buf);
}

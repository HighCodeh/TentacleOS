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

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

#include "subghz_protocol_serializer.h"

static const char *TAG = "SUBGHZ_STORAGE";

#define DECODED_BUF_SIZE  1024
#define RAW_BUF_SIZE      4096

esp_err_t subghz_storage_init(void) {
  ESP_LOGI(TAG, "Storage API Initialized (Placeholder Mode)");
  return ESP_OK;
}

esp_err_t subghz_storage_save_decoded(const char *name, const subghz_data_t *data,
                                      uint32_t frequency, uint32_t te) {
  if (name == NULL || data == NULL) {
    ESP_LOGE(TAG, "Invalid arguments to save_decoded");
    return ESP_ERR_INVALID_ARG;
  }

  char *buf = malloc(DECODED_BUF_SIZE);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate decoded buffer");
    return ESP_ERR_NO_MEM;
  }

  subghz_protocol_serialize_decoded(data, frequency, te, buf, DECODED_BUF_SIZE);

  ESP_LOGI(TAG, "Saving Decoded Signal to %s...", name);
  ESP_LOGI(TAG, "\n--- FILE CONTENT START (%s) ---\n%s--- FILE CONTENT END ---\n", name, buf);

  free(buf);
  return ESP_OK;
}

esp_err_t subghz_storage_save_raw(const char *name, const int32_t *pulses,
                                  size_t count, uint32_t frequency) {
  if (name == NULL || pulses == NULL || count == 0) {
    ESP_LOGE(TAG, "Invalid arguments to save_raw");
    return ESP_ERR_INVALID_ARG;
  }

  char *buf = malloc(RAW_BUF_SIZE);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate raw buffer");
    return ESP_ERR_NO_MEM;
  }

  subghz_protocol_serialize_raw(pulses, count, frequency, buf, RAW_BUF_SIZE);

  ESP_LOGI(TAG, "Saving RAW Signal to %s...", name);
  ESP_LOGI(TAG, "\n--- FILE CONTENT START (%s) ---\n%s--- FILE CONTENT END ---\n", name, buf);

  free(buf);
  return ESP_OK;
}

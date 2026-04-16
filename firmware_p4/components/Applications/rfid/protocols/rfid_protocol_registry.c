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

#include "rfid_protocol_registry.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "RFID_REGISTRY";

extern rfid_protocol_t protocol_em4100;
extern rfid_protocol_t protocol_hid;
extern rfid_protocol_t protocol_hid_corp;
extern rfid_protocol_t protocol_indala;
extern rfid_protocol_t protocol_awid;
extern rfid_protocol_t protocol_fdxb;

static const rfid_protocol_t *REGISTERED_PROTOCOLS[] = {
    &protocol_em4100,
    &protocol_hid,
    &protocol_hid_corp,
    &protocol_indala,
    &protocol_awid,
    &protocol_fdxb,
};

#define REGISTERED_PROTOCOLS_COUNT (sizeof(REGISTERED_PROTOCOLS) / sizeof(REGISTERED_PROTOCOLS[0]))

void rfid_protocol_registry_init(void) {}

bool rfid_protocol_registry_decode_all(const ys_rfid2_raw_data_t *raw,
                                       rfid_decoded_data_t *out_data) {
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; i++) {
    if (REGISTERED_PROTOCOLS[i]->decode != NULL && REGISTERED_PROTOCOLS[i]->decode(raw, out_data)) {
      ESP_LOGD(TAG, "Decoded protocol: %s", REGISTERED_PROTOCOLS[i]->name);
      return true;
    }
  }
  return false;
}

const rfid_protocol_t *rfid_protocol_registry_get_by_name(const char *name) {
  if (name == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; i++) {
    if (strcmp(REGISTERED_PROTOCOLS[i]->name, name) == 0) {
      return REGISTERED_PROTOCOLS[i];
    }
  }
  return NULL;
}

size_t rfid_protocol_registry_get_count(void) {
  return REGISTERED_PROTOCOLS_COUNT;
}

const rfid_protocol_t *rfid_protocol_registry_get_by_index(size_t index) {
  if (index >= REGISTERED_PROTOCOLS_COUNT) {
    return NULL;
  }
  return REGISTERED_PROTOCOLS[index];
}

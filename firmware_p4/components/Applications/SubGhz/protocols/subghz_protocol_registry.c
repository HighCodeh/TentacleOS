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

#include "subghz_protocol_registry.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "PROTOCOL_REGISTRY";

extern subghz_protocol_t protocol_rcswitch;
extern subghz_protocol_t protocol_came;
extern subghz_protocol_t protocol_nice_flo;
extern subghz_protocol_t protocol_princeton;
extern subghz_protocol_t protocol_ansonic;
extern subghz_protocol_t protocol_chamberlain;
extern subghz_protocol_t protocol_holtek;
extern subghz_protocol_t protocol_liftmaster;
extern subghz_protocol_t protocol_linear;
extern subghz_protocol_t protocol_rossi;

static const subghz_protocol_t *REGISTERED_PROTOCOLS[] = {
    &protocol_rcswitch,
    &protocol_came,
    &protocol_nice_flo,
    &protocol_princeton,
    &protocol_ansonic,
    &protocol_chamberlain,
    &protocol_holtek,
    &protocol_liftmaster,
    &protocol_linear,
    &protocol_rossi,
};

#define REGISTERED_PROTOCOLS_COUNT (sizeof(REGISTERED_PROTOCOLS) / sizeof(REGISTERED_PROTOCOLS[0]))

void subghz_protocol_registry_init(void) {}

bool subghz_protocol_registry_decode_all(const int32_t *pulses,
                                         size_t count,
                                         subghz_data_t *out_data) {
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; i++) {
    if (REGISTERED_PROTOCOLS[i]->decode != NULL &&
        REGISTERED_PROTOCOLS[i]->decode(pulses, count, out_data)) {
      ESP_LOGD(TAG, "Decoded protocol: %s", REGISTERED_PROTOCOLS[i]->name);
      return true;
    }
  }
  return false;
}

const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name) {
  if (name == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; i++) {
    if (strcmp(REGISTERED_PROTOCOLS[i]->name, "RCSwitch") == 0 &&
        strstr(name, "RCSwitch") != NULL) {
      return REGISTERED_PROTOCOLS[i];
    }
    if (strcmp(REGISTERED_PROTOCOLS[i]->name, name) == 0) {
      return REGISTERED_PROTOCOLS[i];
    }
  }
  return NULL;
}

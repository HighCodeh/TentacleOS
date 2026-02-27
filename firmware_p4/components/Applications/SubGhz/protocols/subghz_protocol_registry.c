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

// Forward declarations of protocols
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

// Registry of available protocols
static const subghz_protocol_t* protocols[] = {
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

#define PROTOCOL_COUNT (sizeof(protocols) / sizeof(protocols[0]))

void subghz_protocol_registry_init(void) {
    // Initialization logic if needed
}

bool subghz_protocol_registry_decode_all(const int32_t* pulses, size_t count, subghz_data_t* out_data) {
    for (size_t i = 0; i < PROTOCOL_COUNT; i++) {
        if (protocols[i]->decode && protocols[i]->decode(pulses, count, out_data)) {
            return true;
        }
    }
    return false;
}

const subghz_protocol_t* subghz_protocol_registry_get_by_name(const char* name) {
    for (size_t i = 0; i < PROTOCOL_COUNT; i++) {
        // Special case for RCSwitch which has dynamic names in out_data
        if (strcmp(protocols[i]->name, "RCSwitch") == 0 && strstr(name, "RCSwitch") != NULL) {
            return protocols[i];
        }
        if (strcmp(protocols[i]->name, name) == 0) {
            return protocols[i];
        }
    }
    return NULL;
}

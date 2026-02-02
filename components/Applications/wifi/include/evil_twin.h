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


#ifndef EVIL_TWIN_H
#define EVIL_TWIN_H

#include <stdbool.h>
#include <stddef.h>

void evil_twin_start_attack(const char* ssid);
void evil_twin_start_attack_with_template(const char* ssid, const char* template_path);

void evil_twin_stop_attack(void);

void evil_twin_reset_capture(void);
bool evil_twin_has_password(void);
void evil_twin_get_last_password(char *out, size_t len);

#endif // EVIL_TWIN_H


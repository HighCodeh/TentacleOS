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

#ifndef BEACON_SPAM_H
#define BEACON_SPAM_H

#include <stdbool.h>

bool beacon_spam_start_custom(const char *json_path);
bool beacon_spam_start_random(void);
void beacon_spam_stop(void);
bool beacon_spam_is_running(void);

#endif

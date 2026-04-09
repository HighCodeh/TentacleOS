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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Start beacon spam using a custom SSID list from a JSON file.
 *
 * @param json_path  Path to the beacon list JSON file.
 * @return true on success, false on failure.
 */
bool beacon_spam_start_custom(const char *json_path);

/**
 * @brief Start beacon spam with randomly generated SSIDs.
 *
 * @return true on success, false on failure.
 */
bool beacon_spam_start_random(void);

/**
 * @brief Stop the beacon spam attack.
 */
void beacon_spam_stop(void);

/**
 * @brief Check if beacon spam is currently running.
 *
 * @return true if running, false otherwise.
 */
bool beacon_spam_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // BEACON_SPAM_H

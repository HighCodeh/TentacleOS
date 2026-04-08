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

#ifndef TOS_THEME_H
#define TOS_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load and apply the active theme from the SD card.
 *
 * Reads the theme name from the screen config, loads the
 * corresponding theme.conf, and applies colors to the UI.
 */
void tos_theme_load_from_sd(void);

#ifdef __cplusplus
}
#endif

#endif // TOS_THEME_H

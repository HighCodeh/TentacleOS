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

#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Start the system monitor task.
 *
 * Launches a background task pinned to Core 1 that periodically
 * checks all running tasks for critical stack usage. If a task's
 * stack high watermark drops below the threshold, the monitor
 * terminates it and displays a safeguard alert.
 *
 * When verbose logging is enabled, RAM usage (internal + PSRAM)
 * is printed every monitoring cycle.
 *
 * @param is_verbose  Enable periodic RAM usage logging.
 */
void sys_monitor_start(bool is_verbose);

#ifdef __cplusplus
}
#endif

#endif // SYS_MONITOR_H

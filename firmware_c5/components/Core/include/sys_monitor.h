// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Start the background system monitor task.
 *
 * Feeds the Task Watchdog, watches every task's stack high-watermark, and does a
 * controlled restart (never a vTaskDelete) when a task stays critically low for
 * several consecutive cycles.
 *
 * @param is_verbose  Log RAM usage every cycle when true.
 */
void sys_monitor_start(bool is_verbose);

#ifdef __cplusplus
}
#endif

#endif // SYS_MONITOR_H

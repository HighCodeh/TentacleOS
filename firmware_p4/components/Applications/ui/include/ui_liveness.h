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

#ifndef UI_LIVENESS_H
#define UI_LIVENESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Render-progress heartbeat for supervising the LVGL task.
 *
 * A lightweight lv_timer running inside the esp_lvgl_port task increments this
 * counter. It advances only while that task is actually servicing timers, so a
 * frozen renderer (lock deadlock, runaway screen callback, or a dead/suspended
 * task) stalls it. The system monitor polls this instead of matching a task by
 * name: a stalled beat is the real "UI is stuck" signal and triggers a
 * controlled restart.
 *
 * @return Monotonic beat counter. 0 means the UI has not rendered yet (still
 *         booting); the monitor arms its stall check only after it advances.
 */
uint32_t ui_render_beat(void);

#ifdef __cplusplus
}
#endif

#endif // UI_LIVENESS_H

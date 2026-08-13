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

#ifndef UI_SUBGHZ_SCOPE_H
#define UI_SUBGHZ_SCOPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Animated oscilloscope panel, visually identical to the Sub-GHz Read
 *        screen (dark scope frame + accent waveform sweeping across it).
 *
 * Starts an internal animation timer that keeps the trace moving. The timer
 * self-stops once the returned frame object is deleted (e.g. when the host
 * screen is torn down), so callers only need to keep the frame in their tree.
 *
 * @param parent  Parent object (usually the screen).
 * @param align   Alignment of the scope frame within the parent.
 * @param x_ofs   X offset.
 * @param y_ofs   Y offset.
 * @return The scope frame object.
 */
lv_obj_t *subghz_scope_create(lv_obj_t *parent, lv_align_t align, int32_t x_ofs, int32_t y_ofs);

/** @brief Freeze the trace into a steady decoded (OOK) waveform. */
void subghz_scope_lock(void);

/** @brief Stop and release the internal animation timer. */
void subghz_scope_stop(void);

#ifdef __cplusplus
}
#endif

#endif // UI_SUBGHZ_SCOPE_H

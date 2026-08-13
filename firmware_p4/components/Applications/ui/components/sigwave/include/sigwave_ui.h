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

#ifndef UI_SIGWAVE_H
#define UI_SIGWAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief An "IR signal being assembled" animation: a row of vertical pulse bars
 *        (an IR pulse-train / waveform) whose heights rise and fall in a
 *        staggered wave, so the signal looks like it's continuously being
 *        drawn. Looped forever.
 *
 * Creates a self-contained container positioned in `parent` via align/offset.
 * Delete it (or its parent) to stop — the animations go with the objects.
 *
 * @param parent  Container to attach the waveform to.
 * @param align   Alignment of the container within `parent`.
 * @param x_ofs   Horizontal offset from the alignment anchor, in pixels.
 * @param y_ofs   Vertical offset from the alignment anchor, in pixels.
 * @return The container object.
 */
lv_obj_t *sigwave_create(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs);

/**
 * @brief Same pulse-train, but drawn complete and static (no animation) — used
 *        to present a captured signal.
 *
 * @param parent  Container to attach the waveform to.
 * @param align   Alignment of the container within `parent`.
 * @param x_ofs   Horizontal offset from the alignment anchor, in pixels.
 * @param y_ofs   Vertical offset from the alignment anchor, in pixels.
 * @return The container object.
 */
lv_obj_t *sigwave_create_static(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs);

#ifdef __cplusplus
}
#endif

#endif // UI_SIGWAVE_H

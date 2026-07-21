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

#ifndef UI_WAVES_H
#define UI_WAVES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Radar-style pulse: concentric accent rings expanding outward from a
 *        solid central node and fading, looped forever (same effect as the BLE
 *        pairing screen). Used to signal IR receive ("learning") and transmit
 *        activity.
 *
 * Creates a self-contained container positioned in `parent` via align/offset.
 * Delete it (or its parent) to stop — the looping animations are removed with
 * the objects automatically.
 *
 * @param parent     Container to attach the pulse to.
 * @param align      Alignment of the container within `parent`.
 * @param x_ofs      Horizontal offset from the alignment anchor, in pixels.
 * @param y_ofs      Vertical offset from the alignment anchor, in pixels.
 * @param symbol     Optional glyph drawn in the centre node (e.g. LV_SYMBOL_*).
 * @param icon_path  Optional small image asset drawn (scaled down) in the
 *                   centre node; takes precedence over `symbol`. Pass NULL for
 *                   neither.
 * @return The container object.
 */
lv_obj_t *waves_create(lv_obj_t *parent,
                       lv_align_t align,
                       int x_ofs,
                       int y_ofs,
                       const char *symbol,
                       const char *icon_path);

#ifdef __cplusplus
}
#endif

#endif // UI_WAVES_H

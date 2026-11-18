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

#ifndef UI_METRICS_H
#define UI_METRICS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Common motion timing for navigation, separate from hardware/action deadlines.
#define UI_CAROUSEL_TRANSITION_MS 180
#define UI_CONTENT_FADE_MS        120
#define UI_OPEN_GUARD_MS          80

// Live logical screen size. Unlike the LCD_H_RES / LCD_V_RES panel constants
// (fixed 240 x 320), these follow lv_display_set_rotation: in landscape (270)
// they return 320 x 240. Use them for layout that must survive rotation; the
// panel constants stay for DMA buffer sizing only.
static inline int32_t ui_screen_w(void) {
  return lv_display_get_horizontal_resolution(NULL);
}

static inline int32_t ui_screen_h(void) {
  return lv_display_get_vertical_resolution(NULL);
}

#ifdef __cplusplus
}
#endif

#endif // UI_METRICS_H

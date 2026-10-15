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

#ifndef HB_SIGNAL_H
#define HB_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// Shared signal-strength helpers. One place decides how an RSSI (dBm, always
// negative) maps to a color, a quality percentage and a bar count, so the
// spectrum widget, the AP list and any future radio screen stay consistent.

/**
 * @brief Map an RSSI in dBm to a strength color.
 *
 * Green (strong, >= -50) fades through yellow and orange to red (weak,
 * <= -90). Values outside the range are clamped to the endpoints.
 *
 * @param rssi  Signal level in dBm (negative).
 * @return Theme-independent color for the given level.
 */
lv_color_t hb_signal_color(int rssi);

/**
 * @brief Map an RSSI to a 0..100 quality percentage.
 *
 * Uses the same (rssi + 100) * 2 curve the survey already used, clamped to
 * [0, 100], kept here so every screen agrees on the number.
 */
int hb_signal_quality(int rssi);

/**
 * @brief Map an RSSI to 0..4 signal bars.
 */
int hb_signal_bars(int rssi);

#ifdef __cplusplus
}
#endif

#endif // HB_SIGNAL_H

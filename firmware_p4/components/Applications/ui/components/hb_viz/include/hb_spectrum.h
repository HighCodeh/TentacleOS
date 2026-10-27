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

#ifndef HB_SPECTRUM_H
#define HB_SPECTRUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "lvgl.h"

// Channel-occupancy ("spectrum") widget: the classic Wi-Fi analyzer view where
// each network is a translucent tent centered on its channel, its height set by
// signal strength and its width by the channel bandwidth. Overlapping tents show
// congestion at a glance - far more useful than an AP-count-over-time line.
//
// Backed by a single lv_canvas with its own PSRAM draw buffer. The buffer and
// context are freed automatically when the canvas (or its parent screen) is
// deleted, so callers only keep the returned lv_obj_t*.

typedef enum {
  HB_BAND_24 = 0, // 2.4 GHz (channels 1..14)
  HB_BAND_5 = 1,  // 5 GHz   (channels 32..177)
} hb_band_t;

typedef struct {
  uint16_t channel;  // primary channel number
  int16_t rssi;      // signal level in dBm (negative)
  uint8_t width_mhz; // 20 / 40 / 80 / 160; 0 is treated as 20
  const char *label; // optional SSID drawn near the apex; may be NULL
} hb_spectrum_ap_t;

/**
 * @brief Create a spectrum widget as a child of @p parent.
 *
 * @param parent  Parent object.
 * @param w       Canvas width in pixels.
 * @param h       Canvas height in pixels.
 * @return The canvas object, or NULL if allocation failed. Align/position it
 *         like any lv_obj_t. Deleting it (or its parent) frees everything.
 */
lv_obj_t *hb_spectrum_create(lv_obj_t *parent, int32_t w, int32_t h);

/** @brief Choose which band the widget renders. Triggers a redraw. */
void hb_spectrum_set_band(lv_obj_t *spectrum, hb_band_t band);

/** @brief Current band. */
hb_band_t hb_spectrum_get_band(lv_obj_t *spectrum);

/**
 * @brief Replace the set of access points and redraw.
 *
 * The data is copied internally, so @p aps may be a stack buffer. Draw order
 * is as given; pass the array strongest-first so weak tents paint behind
 * strong ones. Only entries in the widget's current band are drawn.
 */
void hb_spectrum_set_aps(lv_obj_t *spectrum, const hb_spectrum_ap_t *aps, int count);

/** @brief Re-render with the current data and band (e.g. after a theme change). */
void hb_spectrum_redraw(lv_obj_t *spectrum);

#ifdef __cplusplus
}
#endif

#endif // HB_SPECTRUM_H
